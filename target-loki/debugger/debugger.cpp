#include "../loki.hpp"
Debugger* debugger = nullptr;

Debugger::Debugger() {
  debugger = this;
  SFC::cpu.debugger.op_exec = {&Debugger::cpuExec, this};
  SFC::cpu.debugger.op_read = {&Debugger::cpuRead, this};
  SFC::cpu.debugger.op_write = {&Debugger::cpuWrite, this};
  SFC::smp.debugger.op_exec = {&Debugger::smpExec, this};
  SFC::smp.debugger.op_read = {&Debugger::smpRead, this};
  SFC::smp.debugger.op_write = {&Debugger::smpWrite, this};
  SFC::ppu.debugger.vram_read = {&Debugger::ppuVramRead, this};
  SFC::ppu.debugger.vram_write = {&Debugger::ppuVramWrite, this};
  SFC::ppu.debugger.oam_read = {&Debugger::ppuOamRead, this};
  SFC::ppu.debugger.oam_write = {&Debugger::ppuOamWrite, this};
  SFC::ppu.debugger.cgram_read = {&Debugger::ppuCgramRead, this};
  SFC::ppu.debugger.cgram_write = {&Debugger::ppuCgramWrite, this};
}

void Debugger::load() {
  directory::create({interface->pathname, "loki/"});

  usageCPU = new uint8[0x1000000]();
  usageAPU = new uint8[0x10000]();
  file fp;

  if(fp.open({interface->pathname, "loki/usage.cpu.bin"}, file::mode::read)) {
    if(fp.size() == 0x1000000) fp.read(usageCPU, 0x1000000);
    fp.close();
  }

  if(fp.open({interface->pathname, "loki/usage.apu.bin"}, file::mode::read)) {
    if(fp.size() == 0x10000) fp.read(usageAPU, 0x10000);
    fp.close();
  }
}

void Debugger::unload() {
  if(tracerFile.open()) tracerFile.close();

  file fp;
  if(fp.open({interface->pathname, "loki/usage.cpu.bin"}, file::mode::write)) {
    fp.write(usageCPU, 0x1000000);
    fp.close();
  }
  if(fp.open({interface->pathname, "loki/usage.apu.bin"}, file::mode::write)) {
    fp.write(usageAPU, 0x10000);
    fp.close();
  }
  delete[] usageCPU;
  delete[] usageAPU;
  usageCPU = nullptr;
  usageAPU = nullptr;
}

void Debugger::main() {
  if(running == false) {
    usleep(20 * 1000);
    return;
  }

  emulator->run();
}

void Debugger::run() {
  running = true;
}

void Debugger::stop() {
  running = false;
  cpuRunFor.reset();
  cpuRunTo.reset();
  cpuStepFor.reset();
  cpuStepTo.reset();
}

void Debugger::leave() {
  stop();
  SFC::scheduler.debug();
}

bool Debugger::breakpointTest(Source source, Breakpoint::Mode mode, unsigned addr, uint8 data) {
  for(unsigned n = 0; n < breakpoints.size(); n++) {
    auto& bp = breakpoints[n];
    if(bp.source != source) continue;
    if(bp.mode != mode) continue;
    if(bp.addr != addr) continue;
    if(bp.mode != Breakpoint::Mode::Execute && bp.data && bp.data() != data) continue;
    echo("Breakpoint #", n, " hit");
    if(bp.mode == Breakpoint::Mode::Read ) echo("; read ",  hex<2>(data));
    if(bp.mode == Breakpoint::Mode::Write) echo("; wrote ", hex<2>(data));
    echo("; triggered: ", ++bp.triggered, "\n");
    return true;
  }
  return false;
}

string Debugger::cpuDisassemble() {
  char text[4096];
  SFC::cpu.disassemble_opcode(text);
  return {text, " I:", (unsigned)SFC::cpu.field(), " V:", format<3>(SFC::cpu.vcounter()), " H:", format<4>(SFC::cpu.hcounter())};
}

string Debugger::cpuDisassemble(unsigned addr, bool e, bool m, bool x) {
  char text[4096];
  SFC::cpu.disassemble_opcode(text, addr, e, m, x);
  return {text, " I:", (unsigned)SFC::cpu.field(), " V:", format<3>(SFC::cpu.vcounter()), " H:", format<4>(SFC::cpu.hcounter())};
}

void Debugger::cpuExec(uint24 addr) {
  usageCPU[addr] |= Usage::Execute;
  if(SFC::cpu.regs.e   == 0) usageCPU[addr] &= ~Usage::FlagE;
  if(SFC::cpu.regs.p.m == 0) usageCPU[addr] &= ~Usage::FlagM;
  if(SFC::cpu.regs.p.x == 0) usageCPU[addr] &= ~Usage::FlagX;
  if(SFC::cpu.regs.e   == 1) usageCPU[addr] |=  Usage::FlagE;
  if(SFC::cpu.regs.p.m == 1) usageCPU[addr] |=  Usage::FlagM;
  if(SFC::cpu.regs.p.x == 1) usageCPU[addr] |=  Usage::FlagX;

  if(tracerFile.open()) {
    if(!tracerMask || tracerMask[addr] == false) {
      if(tracerMask) tracerMask[addr] = true;
      tracerFile.print(cpuDisassemble(), "\n");
    }
  }

  if(breakpointTest(Source::CPU, Breakpoint::Mode::Execute, addr)) {
    echo(cpuDisassemble(), "\n");
    return leave();
  }

  if(cpuRunFor) {
    if(--cpuRunFor() == 0) {
      echo(cpuDisassemble(), "\n");
      return leave();
    }
  }

  if(cpuRunTo) {
    if(addr == cpuRunTo()) {
      echo(cpuDisassemble(), "\n");
      return leave();
    }
  }

  if(cpuStepFor) {
    echo(cpuDisassemble(), "\n");
    if(--cpuStepFor() == 0) return leave();
  }

  if(cpuStepTo) {
    echo(cpuDisassemble(), "\n");
    if(addr == cpuStepTo()) return leave();
  }
}

void Debugger::cpuRead(uint24 addr, uint8 data) {
  usageCPU[addr] |= Usage::Read;
  if(breakpointTest(Source::CPU, Breakpoint::Mode::Read, addr, data)) leave();
}

void Debugger::cpuWrite(uint24 addr, uint8 data) {
  usageCPU[addr] |= Usage::Write;
  if(breakpointTest(Source::CPU, Breakpoint::Mode::Write, addr, data)) leave();
}

void Debugger::echoBreakpoints() {
  if(breakpoints.size() == 0) return;
  echo("#    source  type      addr    data  triggered\n");
  echo("---  ------  --------  ------  ----  ---------\n");
  for(unsigned n = 0; n < breakpoints.size(); n++) {
    auto& bp = breakpoints[n];
    string output = {format<-3>(n), "  "};
    output.append(format<-6>(sourceName(bp.source)), "  ");
    if(bp.mode == Breakpoint::Mode::Disabled) output.append("disabled  ");
    if(bp.mode == Breakpoint::Mode::Read    ) output.append("read      ");
    if(bp.mode == Breakpoint::Mode::Write   ) output.append("write     ");
    if(bp.mode == Breakpoint::Mode::Execute ) output.append("execute   ");
    output.append(hex<6>(bp.addr), "  ");
    output.append(bp.data ? hex<2>(bp.data()) : "  ", "    ");
    output.append(format<-9>(bp.triggered));
    echo(output, "\n");
  }
}

void Debugger::echoDisassemble(unsigned addr, signed size) {
  if(!(usageCPU[addr] & Usage::Execute)) return echo("No usage data available for cpu/", hex<6>(addr), "\n");

  while(size > 0) {
    string text = cpuDisassemble(addr, usageCPU[addr] & Usage::FlagE, usageCPU[addr] & Usage::FlagM, usageCPU[addr] & Usage::FlagX);
    text.resize(20);  //remove register information
    echo(text, "\n");
    if(--size <= 0) break;

    unsigned displacement = 1;
    while(displacement < 5) {  //maximum opcode length is four bytes
      if(usageCPU[addr + displacement] & Usage::Execute) break;
      displacement++;
    }
    if(displacement >= 5) {
      echo("...\n");
      return;
    }
    addr += displacement;
  }
}

void Debugger::echoHex(Source source, unsigned addr, signed size) {
  while(size > 0) {
    string hexdata, asciidata;
    for(unsigned n = 0; n < 16; n++) {
      unsigned offset = addr;
      if(source == Source::CPU && ((offset & 0x40e000) == 0x002000 || (offset & 0x40e000) == 0x004000)) {
        //$00-3f,80-bf:2000-5fff
        //reading MMIO registers can negatively impact emulation, so disallow these reads
        hexdata.append("?? ");
        asciidata.append("?");
      } else {
        uint8 byte = memoryRead(source, addr + n);
        hexdata.append(hex<2>(byte), " ");
        asciidata.append(byte >= 0x20 && byte <= 0x7e ? (char)byte : '.');
      }
    }
    echo(hex<6>(addr % memorySize(source)), " [ ", hexdata, "] ", asciidata, "\n");
    addr += 16, size -= 16;
  }
}

void Debugger::memoryExport(Source source, string filename) {
  file fp;
  if(fp.open(filename, file::mode::write)) {
    unsigned size = memorySize(source);
    for(unsigned addr = 0; addr < size; addr++) {
      fp.write(memoryRead(source, addr));
    }
    echo("Exported memory to ", notdir(filename), "\n");
  }
}

uint8 Debugger::memoryRead(Source source, unsigned addr) {
  if(source == Source::CPU) {
    return SFC::bus.read(addr & 0xffffff);
  }

  if(source == Source::APU) {
    return SFC::smp.apuram[addr & 0xffff];
  }

  if(source == Source::VRAM) {
    return SFC::ppu.vram[addr & 0xffff];
  }

  if(source == Source::OAM) {
    return SFC::ppu.oam[addr % 544];
  }

  if(source == Source::CGRAM) {
    return SFC::ppu.cgram[addr & 511];
  }

  return 0x00;
}

unsigned Debugger::memorySize(Source source) {
  switch(source) {
  case Source::CPU: return 0x1000000;
  case Source::APU: return 0x10000;
  case Source::VRAM: return 0x10000;
  case Source::OAM: return 544;
  case Source::CGRAM: return 512;
  }
  return 1;
}

void Debugger::memoryWrite(Source source, unsigned addr, uint8 data) {
  if(source == Source::CPU) {
    SFC::bus.write(addr & 0xffffff, data);
    return;
  }

  if(source == Source::APU) {
    SFC::smp.apuram[addr & 0xffff] = data;
    return;
  }

  if(source == Source::VRAM) {
    SFC::ppu.vram[addr & 0xffff] = data;
    return;
  }

  if(source == Source::OAM) {
    SFC::ppu.oam[addr % 544] = data;
    SFC::ppu.sprite.update(addr % 544, data);
    return;
  }

  if(source == Source::CGRAM) {
    if(addr & 1) data &= 0x7f;
    SFC::ppu.cgram[addr] = data;
    return;
  }
}

void Debugger::ppuCgramRead(uint16 addr, uint8 data) {
  if(breakpointTest(Source::CGRAM, Breakpoint::Mode::Read, addr, data)) leave();
}

void Debugger::ppuCgramWrite(uint16 addr, uint8 data) {
  if(breakpointTest(Source::CGRAM, Breakpoint::Mode::Write, addr, data)) leave();
}

void Debugger::ppuOamRead(uint16 addr, uint8 data) {
  if(breakpointTest(Source::OAM, Breakpoint::Mode::Read, addr, data)) leave();
}

void Debugger::ppuOamWrite(uint16 addr, uint8 data) {
  if(breakpointTest(Source::OAM, Breakpoint::Mode::Write, addr, data)) leave();
}

void Debugger::ppuVramRead(uint16 addr, uint8 data) {
  if(breakpointTest(Source::VRAM, Breakpoint::Mode::Read, addr, data)) leave();
}

void Debugger::ppuVramWrite(uint16 addr, uint8 data) {
  if(breakpointTest(Source::VRAM, Breakpoint::Mode::Write, addr, data)) leave();
}

void Debugger::smpExec(uint16 addr) {
  usageAPU[addr] |= Usage::Execute;

  if(breakpointTest(Source::SMP, Breakpoint::Mode::Execute, addr)) {
    leave();
  }
}

void Debugger::smpRead(uint16 addr, uint8 data) {
  usageAPU[addr] |= Usage::Read;
  if(breakpointTest(Source::SMP, Breakpoint::Mode::Read, addr, data)) leave();
  if(breakpointTest(Source::APU, Breakpoint::Mode::Read, addr, data)) leave();
}

void Debugger::smpWrite(uint16 addr, uint8 data) {
  usageAPU[addr] |= Usage::Write;
  if(breakpointTest(Source::SMP, Breakpoint::Mode::Write, addr, data)) leave();
  if(breakpointTest(Source::APU, Breakpoint::Mode::Write, addr, data)) leave();
}

string Debugger::sourceName(Source source) {
  switch(source) {
  case Source::CPU: return "cpu";
  case Source::SMP: return "smp";
  case Source::PPU: return "ppu";
  case Source::DSP: return "dsp";
  case Source::APU: return "apu";
  case Source::VRAM: return "vram";
  case Source::OAM: return "oam";
  case Source::CGRAM: return "cgram";
  }
  return "none";
}

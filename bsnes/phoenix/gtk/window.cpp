static void Action_setFont(GtkWidget *widget, gpointer font);
static void Widget_setFont(GtkWidget *widget, gpointer font);

static gint Window_close(Window *window) {
  if(window->onClose) window->onClose();
  window->setVisible(false);
  return true;
}

static gboolean Window_configure(GtkWindow *widget, GdkEvent *event, Window *window) {
  if(gtk_widget_get_realized(window->p.widget) == false) return false;
  window->p.updateFrameGeometry();

  signed eventX = event->configure.x, eventY = event->configure.y;
  unsigned eventWidth = event->configure.width, eventHeight = event->configure.height;

  if(window->p.locked == false && window->state.fullScreen == false) {
    if(window->state.geometry.x != eventX || window->state.geometry.y != eventY) {
      window->state.geometry.x = eventX;
      window->state.geometry.y = eventY;
    }
  }

  if(window->onMove) window->onMove();

  eventHeight -= window->state.menuVisible ? window->p.menu->allocation.height : 0;
  eventHeight -= window->state.statusVisible ? window->p.status->allocation.height : 0;

  if(window->p.locked == false && window->state.fullScreen == false) {
    if(window->state.geometry.width != eventWidth || window->state.geometry.height != eventHeight) {
      window->state.geometry.width = eventWidth;
      window->state.geometry.height = eventHeight;
    }
  }

  foreach(layout, window->state.layout) {
    Geometry geometry = window->geometry();
    geometry.x = geometry.y = 0;
    layout.setGeometry(geometry);
  }

  if(window->onSize) window->onSize();
  return false;
}

void pWindow::append(Layout &layout) {
  layout.setParent(window);
  Geometry geometry = this->geometry();
  geometry.x = geometry.y = 0;
  layout.setGeometry(geometry);
}

void pWindow::append(Menu &subMenu) {
  if(window.state.menuFont) subMenu.p.setFont(*window.state.menuFont);
  gtk_menu_bar_append(menu, subMenu.p.widget);
  gtk_widget_show(subMenu.p.widget);
}

void pWindow::append(Widget &widget) {
  widget.p.parentWindow = this;
  if(!widget.state.font && window.state.widgetFont) {
    widget.setFont(*window.state.widgetFont);
  }
  gtk_fixed_put(GTK_FIXED(formContainer), widget.p.gtkWidget, 0, 0);
  widget.setVisible();
}

Geometry pWindow::frameGeometry() {
  if(window.state.fullScreen == true) return { 0, 0, OS::desktopWidth(), OS::desktopHeight() };
  return {
    window.state.geometry.x - settings.frameGeometryX,
    window.state.geometry.y - settings.frameGeometryY,
    window.state.geometry.width + settings.frameGeometryWidth,
    window.state.geometry.height + settings.frameGeometryHeight
  };
}

bool pWindow::focused() {
  return gtk_window_is_active(GTK_WINDOW(widget));
}

Geometry pWindow::geometry() {
  if(window.state.fullScreen == true) {
    unsigned menuHeight = 0, statusHeight = 0;
    if(window.state.menuVisible) menuHeight = menu->allocation.height;
    if(window.state.statusVisible) statusHeight = menu->allocation.height;
    return { 0, menuHeight, OS::desktopWidth(), OS::desktopHeight() - menuHeight - statusHeight };
  }
  return window.state.geometry;
}

void pWindow::setBackgroundColor(uint8_t red, uint8_t green, uint8_t blue) {
  GdkColor color;
  color.pixel = (red << 16) | (green << 8) | (blue << 0);
  color.red = (red << 8) | (red << 0);
  color.green = (green << 8) | (green << 0);
  color.blue = (blue << 8) | (blue << 0);
  gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &color);
}

void pWindow::setFrameGeometry(const Geometry &geometry) {
  window.setGeometry({
    geometry.x + settings.frameGeometryX, geometry.y + settings.frameGeometryY,
    geometry.width - settings.frameGeometryWidth, geometry.height - settings.frameGeometryHeight
  });
}

void pWindow::setFocused() {
  gtk_window_present(GTK_WINDOW(widget));
}

void pWindow::setFullScreen(bool fullScreen) {
  if(fullScreen == false) {
    gtk_window_unfullscreen(GTK_WINDOW(widget));
    gtk_window_set_resizable(GTK_WINDOW(widget), window.state.resizable);
    gtk_window_set_decorated(GTK_WINDOW(widget), true);
    locked = true;
    for(unsigned n = 0; n < 4; n++) {
      setGeometry(window.state.geometry);
      gtk_widget_set_size_request(widget, -1, -1);
      OS::process();
      usleep(2000);
    }
    locked = false;
  } else {
    gtk_window_fullscreen(GTK_WINDOW(widget));
    gtk_window_set_decorated(GTK_WINDOW(widget), false);
    gtk_widget_set_size_request(widget, OS::desktopWidth(), OS::desktopHeight());
    gtk_window_set_resizable(GTK_WINDOW(widget), false);
  }
}

void pWindow::setGeometry(const Geometry &geometry) {
  gtk_window_move(GTK_WINDOW(widget), geometry.x - settings.frameGeometryX, geometry.y - settings.frameGeometryY);
  gtk_window_resize(GTK_WINDOW(widget), 1, 1);
  gtk_widget_set_size_request(formContainer, geometry.width, geometry.height);
  foreach(layout, window.state.layout) {
    Geometry geometry = this->geometry();
    geometry.x = geometry.y = 0;
    layout.setGeometry(geometry);
  }
}

void pWindow::setMenuFont(Font &font) {
  foreach(item, window.state.menu) item.p.setFont(font);
}

void pWindow::setMenuVisible(bool visible) {
  gtk_widget_set_visible(menu, visible);
}

void pWindow::setResizable(bool resizable) {
  gtk_window_set_resizable(GTK_WINDOW(widget), resizable);
  gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(status), resizable);
}

void pWindow::setStatusFont(Font &font) {
  Widget_setFont(status, (gpointer)font.p.gtkFont);
}

void pWindow::setStatusText(const string &text) {
  gtk_statusbar_pop(GTK_STATUSBAR(status), 1);
  gtk_statusbar_push(GTK_STATUSBAR(status), 1, text);
}

void pWindow::setStatusVisible(bool visible) {
  gtk_widget_set_visible(status, visible);
}

void pWindow::setTitle(const string &text) {
  gtk_window_set_title(GTK_WINDOW(widget), text);
}

void pWindow::setVisible(bool visible) {
  gtk_widget_set_visible(widget, visible);
}

void pWindow::setWidgetFont(Font &font) {
  foreach(item, window.state.widget) {
    if(!item.state.font) item.setFont(font);
  }
}

void pWindow::constructor() {
  widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_resizable(GTK_WINDOW(widget), true);
  gtk_widget_set_app_paintable(widget, true);
  gtk_widget_add_events(widget, GDK_CONFIGURE);

  menuContainer = gtk_vbox_new(false, 0);
  gtk_container_add(GTK_CONTAINER(widget), menuContainer);
  gtk_widget_show(menuContainer);

  menu = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(menuContainer), menu, false, false, 0);

  formContainer = gtk_fixed_new();
  gtk_box_pack_start(GTK_BOX(menuContainer), formContainer, true, true, 0);
  gtk_widget_show(formContainer);

  statusContainer = gtk_event_box_new();
  status = gtk_statusbar_new();
  gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(status), true);
  gtk_container_add(GTK_CONTAINER(statusContainer), status);
  gtk_box_pack_start(GTK_BOX(menuContainer), statusContainer, false, false, 0);
  gtk_widget_show(statusContainer);

  setTitle("");
  setGeometry(window.state.geometry);

  g_signal_connect_swapped(G_OBJECT(widget), "delete-event", G_CALLBACK(Window_close), (gpointer)&window);
  g_signal_connect(G_OBJECT(widget), "configure-event", G_CALLBACK(Window_configure), (gpointer)&window);
}

void pWindow::updateFrameGeometry() {
  #if defined(PLATFORM_X)
  Display *display = XOpenDisplay(0);
  XWindowAttributes attributes, parentAttributes;
  XGetWindowAttributes(display, GDK_WINDOW_XID(widget->window), &attributes);
  X11Window rootWindow, parentWindow, *childWindow = 0;
  unsigned int childCount;
  XQueryTree(display, GDK_WINDOW_XID(widget->window), &rootWindow, &parentWindow, &childWindow, &childCount);
  XGetWindowAttributes(display, parentWindow, &parentAttributes);
  if(childWindow) XFree(childWindow);
  XCloseDIsplay(display);

  settings.frameGeometryX = attributes.x;
  settings.frameGeometryY = attributes.y;
  settings.frameGeometryWidth = parentAttributes.width - attributes.width;
  settings.frameGeometryHeight = parentAttributes.height - attributes.height;
  #endif
}
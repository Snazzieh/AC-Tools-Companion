#!/usr/bin/env python3
import os
import sys

import gi


def import_gtk_webkit():
    errors = []

    for gtk_version, webkit_namespace, webkit_version in (
        ("4.0", "WebKit", "6.0"),
        ("3.0", "WebKit2", "4.1"),
        ("3.0", "WebKit2", "4.0"),
    ):
        try:
            gi.require_version("Gtk", gtk_version)
            gi.require_version(webkit_namespace, webkit_version)
            from gi.repository import Gtk
            webkit = __import__("gi.repository", fromlist=[webkit_namespace])
            return Gtk, getattr(webkit, webkit_namespace), gtk_version
        except (ImportError, ValueError) as exc:
            errors.append(f"Gtk {gtk_version} + {webkit_namespace} {webkit_version}: {exc}")

    details = "\n".join(errors)
    raise SystemExit(
        "No GTK WebKit bindings found.\n"
        "Try installing one of these on the Pi:\n"
        "  sudo apt install -y python3-gi gir1.2-gtk-4.0 gir1.2-webkit-6.0\n"
        "  sudo apt install -y python3-gi gir1.2-gtk-3.0 gir1.2-webkit2-4.1\n\n"
        f"Import attempts:\n{details}"
    )


Gtk, WebKit, GTK_VERSION = import_gtk_webkit()


def configure_webview(webview):
    settings = webview.get_settings()
    for name, value in (
        ("enable-developer-extras", False),
        ("enable-page-cache", True),
        ("enable-javascript", True),
        ("enable-webgl", False),
        ("enable-plugins", False),
        ("enable-mediasource", False),
        ("allow-modal-dialogs", False),
    ):
        try:
            settings.set_property(name, value)
        except TypeError:
            pass


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.254.1/"
    os.environ.setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1")

    if GTK_VERSION.startswith("4"):
        app = Gtk.Application(application_id="local.accesspi.webview")

        def activate(application):
            window = Gtk.ApplicationWindow(application=application)
            window.fullscreen()
            webview = WebKit.WebView()
            configure_webview(webview)
            window.set_child(webview)
            window.present()
            webview.load_uri(url)

        app.connect("activate", activate)
        return app.run([])

    window = Gtk.Window()
    window.fullscreen()
    window.connect("destroy", Gtk.main_quit)

    webview = WebKit.WebView()
    configure_webview(webview)
    window.add(webview)
    window.show_all()
    webview.load_uri(url)
    Gtk.main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

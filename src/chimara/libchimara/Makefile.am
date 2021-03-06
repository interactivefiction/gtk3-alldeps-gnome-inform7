## Process this file with automake to produce Makefile.in

AM_CFLAGS = -Wall

lib_LTLIBRARIES = libchimara.la

libchimara_la_SOURCES = \
	abort.c abort.h \
	case.c \
	charset.c charset.h \
	chimara-glk.c chimara-glk.h chimara-glk-private.h \
	chimara-if.c chimara-if.h \
	chimara-marshallers.c chimara-marshallers.h \
	datetime.c \
	dispatch.c \
	event.c event.h \
	fileref.c fileref.h \
	garglk.c garglk.h \
	gestalt.c \
	graphics.c graphics.h \
	gi_blorb.c gi_blorb.h \
	gi_dispa.c gi_dispa.h \
	glk.c glk.h \
	glkstart.h \
	glkunix.c glkunix.h \
	hyperlink.c \
	init.c init.h \
	input.c input.h \
	magic.c magic.h \
	mouse.c \
	pager.c pager.h \
	resource.c resource.h \
	schannel.c schannel.h \
	stream.c stream.h \
	strio.c strio.h \
	style.c style.h \
	timer.c \
	ui-buffer.c ui-buffer.h \
	ui-graphics.c ui-graphics.h \
	ui-grid.c ui-grid.h \
	ui-message.c ui-message.h \
	ui-misc.c ui-misc.h \
	ui-style.c ui-style.h \
	ui-textwin.c ui-textwin.h \
	ui-window.c ui-window.h \
	window.c window.h \
	$(NULL)
libchimara_la_CPPFLAGS = $(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"Chimara\" \
	-DLOCALEDIR=\""$(datadir)/locale"\" \
	-DPLUGINDIR=\""$(pkglibdir)"\" \
	-DPLUGINSOURCEDIR=\""$(abs_builddir)/../interpreters"\" \
	-I$(top_srcdir)
libchimara_la_CFLAGS = @CHIMARA_CFLAGS@ $(AM_CFLAGS)
libchimara_la_LIBADD = @CHIMARA_LIBS@
libchimara_la_LDFLAGS = -version-info $(LT_VERSION_INFO) \
	-no-undefined \
	-export-symbols-regex "^(glk|chimara|glkunix|giblorb|gidispatch|garglk)_"
libchimara_includedir = $(includedir)/chimara/libchimara
libchimara_include_HEADERS = \
	chimara-glk.h \
	chimara-if.h \
	glk.h \
	glkstart.h \
	gi_blorb.h \
	gi_dispa.h \
	garglk.h

BUILT_SOURCES = chimara-marshallers.c chimara-marshallers.h
chimara-marshallers.c: chimara-marshallers.txt
	$(AM_V_GEN)glib-genmarshal --body --prefix=_chimara_marshal $< > $@

chimara-marshallers.h: chimara-marshallers.txt
	$(AM_V_GEN)glib-genmarshal --header --prefix=_chimara_marshal $< > $@

EXTRA_DIST = doc.c glkstart.c chimara-marshallers.txt
CLEANFILES =

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
# The '--shared-library' incantation is because, for some reason, the typelib
# only stores the filename without a pathname (see, in gobject-introspection,
# _g_typelib_do_dlopen() in gitypelib.c around line 2100.) This is a regression
# in either gobject-introspection or g-ir-compiler, but I can't pinpoint it.
INTROSPECTION_COMPILER_ARGS = \
	--includedir=$(srcdir) \
	--shared-library=$(libdir)/libchimara.so.0

if HAVE_INTROSPECTION
introspection_sources = \
	chimara-glk.c chimara-glk.h \
	chimara-if.c chimara-if.h

Chimara-1.0.gir: libchimara.la
Chimara_1_0_gir_INCLUDES = GObject-2.0 GLib-2.0 Gtk-3.0
Chimara_1_0_gir_CFLAGS = @CHIMARA_CFLAGS@ -I$(top_srcdir)
Chimara_1_0_gir_LIBS = libchimara.la
Chimara_1_0_gir_FILES = $(introspection_sources)
Chimara_1_0_gir_SCANNERFLAGS = \
	--warn-all \
	--c-include='libchimara/chimara-glk.h' \
	--c-include='libchimara/chimara-if.h'
Chimara_1_0_gir_EXPORT_PACKAGES = chimara
INTROSPECTION_GIRS += Chimara-1.0.gir

girdir = $(subst /usr,$(prefix),@INTROSPECTION_GIRDIR@)
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(subst /usr,$(prefix),@INTROSPECTION_TYPELIBDIR@)
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif
GITIGNOREFILES = Chimara-1.0.gir Chimara-1.0.typelib

vapidir = $(datadir)/vala/vapi
dist_vapi_DATA = chimara.vapi

if BUILDING_VAPI
chimara.vapi: $(INTROSPECTION_GIRS)
	$(AM_V_GEN)$(VAPIGEN) --library=chimara --pkg gtk+-3.0 Chimara-1.0.gir && \
	touch $@
else
chimara.vapi: $(INTROSPECTION_GIRS)
	touch $@
	@echo "WARNING: chimara.vapi not generated; install vala-tools if you"
	@echo "want to be able to run distcheck or make release tarballs."
dist-hook:
	@echo "WARNING: install vala-tools if you want to be able to run"
	@echo "distcheck or make release tarballs."
	exit 1
endif

DISTCLEANFILES = chimara.vapi
-include $(top_srcdir)/git.mk

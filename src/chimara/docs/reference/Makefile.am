### Makefile.am for Gtk-Doc directory #########################################
# Adapted from examples/Makefile.am in Gtk-Doc distribution

# The name of the module, e.g. 'glib'.
DOC_MODULE = chimara

# Uncomment for versioned docs and specify the version of the module, e.g. '2'.
# DOC_MODULE_VERSION = 2

# The top-level XML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml

# Directories containing the source code.
# gtk-doc will search all .c & .h files beneath these paths
# for inline comments documenting the functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/libchimara

# Extra options to pass to gtkdoc-scangobj. Normally not needed.
SCANGOBJ_OPTIONS = 

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS = --deprecated-guards="GTK_DISABLE_DEPRECATED"
# DO NOT use --rebuild-types, because it will think glk_window_get_type()
# is a GObject type. That makes it segfault.
SCAN_OPTIONS = 

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS = --sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mkhtml
MKHTML_OPTIONS = 

# Extra options to supply to gtkdoc-fixref. Normally not needed.
# e.g. FIXXREF_OPTIONS = --extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS = 

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/libchimara/*.h
CFILE_GLOB = $(top_srcdir)/libchimara/*.c

# Extra headers to include when scanning, which are not under DOC_SOURCE_DIR
# e.g. EXTRA_HFILES=$(top_srcdir)/contrib/extra.h
EXTRA_HFILES = 

# Header files or dirs to ignore when scanning. Use base file/dir names
# e.g. IGNORE_HFILES=gtkdebug.h gtkintl.h private_code
IGNORE_HFILES = chimara-glk-private.h chimara-marshallers.h \
	abort.h charset.h event.h fileref.h graphics.h hyperlink.h init.h input.h \
	magic.h mouse.h pager.h resource.h schannel.h stream.h strio.h style.h \
	timer.h ui-message.h window.h

# Images to copy into HTML directory.
HTML_IMAGES = \
	images/fig1.png \
	images/fig2.png \
	images/fig3a.png \
	images/fig3b.png \
	images/fig3c.png \
	images/fig4a.png \
	images/fig4b.png \
	images/fig4c.png \
	images/fig5-7a.png \
	images/fig6.png \
	images/fig7b.png \
	images/fig7c.png \
	images/fig7d.png \
	images/fig7e.png \
	images/fig8a.png \
	images/fig8b.png \
	images/fig8c.png \
	images/fig9.png \
	images/fig10.png \
	images/fig11.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = version.xml \
	selectors.xml \
	glk-front-matter.sgml \
	glk-introduction.sgml \
	glk-main-function.sgml \
	glk-api-conventions.sgml \
	glk-character-encoding.sgml \
	glk-normalization.sgml \
	glk-output.sgml \
	glk-line-input.sgml \
	glk-windows.sgml \
	glk-window-arrangement.sgml \
	glk-display-style.sgml \
	glk-other-events.sgml \
	glk-sound-resources.sgml \
	glk-porting.sgml \
	dispatch.sgml \
	dispatch-selectors.sgml \
	blorb.sgml \
	blorb-library.sgml

# Files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
expand_content_files = \
	glk-main-function.sgml \
	glk-api-conventions.sgml \
	glk-normalization.sgml \
	glk-output.sgml \
	glk-line-input.sgml \
	glk-window-arrangement.sgml \
	glk-display-style.sgml \
	glk-other-events.sgml

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
GTKDOC_CFLAGS = -I$(top_srcdir) $(CHIMARA_CFLAGS)
GTKDOC_LIBS =  $(top_builddir)/libchimara/libchimara.la $(CHIMARA_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += version.xml.in

# Files not to distribute
# for --rebuild-types in $(SCAN_OPTIONS), e.g. $(DOC_MODULE).types
# for --rebuild-sections in $(SCAN_OPTIONS), e.g. $(DOC_MODULE)-sections.txt
CLEANFILES += selectors.xml
DISTCLEANFILES = version.xml $(DOC_MODULE)-overrides.txt

# Uncomment this if you want your docs-status tested during 'make check'
# Comment this while not all symbols are documented, otherwise distcheck fails
if ENABLE_GTK_DOC
TESTS_ENVIRONMENT = \
	DOC_MODULE=$(DOC_MODULE) DOC_MAIN_SGML_FILE=$(DOC_MAIN_SGML_FILE) \
	SRCDIR=$(abs_srcdir) BUILDDIR=$(abs_builddir)
TESTS = $(GTKDOC_CHECK)
endif

dist_noinst_SCRIPTS = build-selector-table.pl

selectors.xml: $(srcdir)/../../libchimara/gi_dispa.c
	$(AM_V_GEN)$(PERL) build-selector-table.pl $< > $@


-include $(top_srcdir)/git.mk

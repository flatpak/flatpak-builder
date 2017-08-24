NULL =

XSLTPROC_FLAGS = \
	--nonet						\
	--stringparam man.output.quietly 1		\
	--stringparam funcsynopsis.style ansi		\
	--stringparam man.th.extra1.suppress 1		\
	--stringparam man.authors.section.enabled 0	\
	--stringparam man.copyright.section.enabled 0	\
	$(NULL)

.xml.1:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

.xml.5:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

man1 = \
	flatpak-builder.1		\
	$(NULL)

man5 = \
	flatpak-manifest.5		\
	$(NULL)

man_MANS = \
	$(man1) \
	$(man5) \
	$(NULL)

xml_files = \
	$(man1:.1=.xml) \
	$(man5:.5=.xml) \
	$(NULL)

EXTRA_DIST = \
	$(xml_files) 		\
	docbook.css		\
	flatpak-builder-docs.xml.in	\
	xmlto-config.xsl	\
	$(NULL)

CLEANFILES = $(NULL)
DISTCLEANFILES = \
	$(man_MANS)		\
	flatpak-builder-docs.xml	\
	$(NULL)

if DOCBOOK_DOCS_ENABLED

doc_DATA =                      \
        flatpak-builder-docs.html	\
        docbook.css             \
        $(NULL)

CLEANFILES += flatpak-builder-docs.html

flatpak-builder-docs.html: flatpak-builder-docs.xml $(xml_files) xmlto-config.xsl
	$(AM_V_GEN) $(XMLTO) $(XMLTO_FLAGS) --skip-validation xhtml-nochunks -m $(srcdir)/xmlto-config.xsl $<

endif # DOCBOOK_DOCS_ENABLED


SUBDIRS = icons
@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_RULE@

dist_man_MANS = guvcview.1

CLEANFILES = \
		$(menu_DATA)\
		$(desktop_DATA) \
		$(appdata_DATA)

EXTRA_DIST = \
		$(APPDATA_IN_FILES) \
		$(DESKTOP_IN_FILES)\
		$(MENU_IN_FILES)

clean-generic:
	test -z "$(CONFIG_CLEAN_FILES)" || rm -f $(CONFIG_CLEAN_FILES); \
	test -z "$(CLEANFILES)" || rm -f $(CLEANFILES)

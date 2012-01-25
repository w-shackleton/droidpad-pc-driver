_LNG=$(LANGUAGE)

# Copy hidkmdf.sys to sub-directory of install 
$(INSTALL_DIR)\$(O)\$(TARGETNAME).sys: $(OBJ_PATH)\$O\$(TARGETNAME).sys
	if not exist $(INSTALL_DIR)\$(O) mkdir $(INSTALL_DIR)\$(O)
	copy $(OBJ_PATH)\$O\$(TARGETNAME).sys  $@

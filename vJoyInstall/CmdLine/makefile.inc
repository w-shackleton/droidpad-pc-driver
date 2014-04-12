_LNG=$(LANGUAGE)

####################################################################
# Use the correct  manifest file
!if "$(_BUILDARCH)" == "AMD64"
MANIFEST=vJoyInstallx64.exe.manifest
!else
MANIFEST=vJoyInstallx86.exe.manifest
!endif
####################################################################

# Mark file to "run as" administrator
AS_ADMIN=mt.exe -manifest "$(MANIFEST)" -outputresource:$(OBJ_PATH)\$O\$(TARGETNAME).exe
 
# Copy installer to sub-directory of install 
$(INSTALL_DIR)\$(O)\$(TARGETNAME).exe: $(OBJ_PATH)\$O\$(TARGETNAME).exe
	$(AS_ADMIN)
	if not exist $(INSTALL_DIR)\$(O) mkdir $(INSTALL_DIR)\$(O)
# signtool sign /v /s PrivateCertStore $(OBJ_PATH)\$O\$(TARGETNAME).exe
	copy $(OBJ_PATH)\$O\$(TARGETNAME).exe  $@


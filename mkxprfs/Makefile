BUILDPATH = ./

$(BUILDPATH)/mkxprfs : bitops.h xpr_fs.h mkxprfs.c
	gcc mkxprfs.c -o mkxprfs
	
default:
	make -r $(BUILDPATH)/mkxprfs
	@echo "Compile Success"
install:
	make default
	cp mkxprfs /sbin/mkxprfs
	ln -s /sbin/mkxprfs /sbin/mkfs.xprfs
	@echo "Install success"
uninstall:
	rm /sbin/mkxprfs /sbin/mkfs.xprfs
	@echo "Uninstall success"
clean:
	rm mkxprfs
	@echo "Clean success"
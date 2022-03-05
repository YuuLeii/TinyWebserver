MD = mkdir -p
OUTPUTDIR = bin
target:
	$(MD) $(OUTPUTDIR)
	$(MAKE) -C src OUTPUTDIR=$(shell pwd)/$(OUTPUTDIR)
clean:
	@ rm -rf bin
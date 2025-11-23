
LIBNAME=libTinyGL.a
LIB=lib/$(LIBNAME)
LIBDIR=/usr/local/lib
INCDIR=/usr/local/include
BINDIR=/usr/local/bin

all: $(LIB) RDMOS
	@echo Done!

$(LIB):
	mkdir -p lib
	cd src && $(MAKE) && cd ..
	cp src/*.a ./lib/

install: $(LIB)
	cp $(LIB) $(LIBDIR)
	mkdir $(INCDIR)/tinygl || echo "You installed before?"
	cp -r include/* $(INCDIR)/tinygl

uninstall:
	rm -f $(LIBDIR)/$(LIBNAME)
	rm -rf $(INCDIR)/tinygl

SDL_Examples: $(LIB)
	@echo "These demos require SDL 1.2 to compile."
	cd SDL_Examples && $(MAKE) && cd ..

tglgears: $(LIB)
	@echo "These demos require SDL 1.2 to compile."
	cd SDL_Examples && $(MAKE) install_tglgears && cd ..

RDMOS: $(LIB)
	@echo "Building the RAW DEMOS. These do not require anything special on your system, so they should succeed."
	cd Raw_Demos && $(MAKE) && cd ..
	
clean:
	cd src && $(MAKE) clean && cd ..
	cd SDL_Examples && $(MAKE) clean && cd ..
	cd Raw_Demos && $(MAKE) clean && cd ..
	rm -rf lib
#clean:
#	rm -f *~ *.o *.a
#	cd SDL_Examples && $(MAKE) clean && cd ..

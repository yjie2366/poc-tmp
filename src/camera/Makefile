OS=$(shell uname -s)

ifeq ($(OS), Darwin)
INCLUDES=-I/opt/homebrew/include -I./include
LIB=/opt/homebrew/lib
else
INCLUDES=-I/usr/local/include -I./include -I$(HOME)/install/include/ 
LIB=-L/usr/local/lib -L$(HOME)/install/lib
endif

.PHONY: all clean

all: plugin-camera-test-exe subscriber filter sysname.h

subscriber: subscriber.c mqtt_testlib.c
	$(CC) $(INCLUDES) -Wall -O2 -o $@ $^ $(LIB) -lmosquitto 

filter: filter.c mqtt_testlib.c
	$(CC) $(INCLUDES) -Wall -O2 -o $@ $^ $(LIB) -lmosquitto
	sudo cp $@ /usr/local/sbin

gst_camera: camera-fdsrc.c
	$(CC) -Wall -O2 $^ -o $@ $(shell pkg-config --cflags --libs gstreamer-1.0) -li2c -lpthread

sysname.h: sysname_gen.c sysname.incl
	$(CC) -o sysname_gen sysname_gen.c
	@./sysname_gen > $@

sysname.incl:
	ausyscall --dump | sed -f script.sed | sed -e 's/Using.*//'> sysname.incl

plugin-camera-test: gst_camera sysname.h plugin-libcamera-vid.c mqtt_testlib.c plugin-testlib.c
	$(CC) $(INCLUDES) -Wall -O2 -o $@ $^ $(LIB) -lauparse -laudit -lmosquitto 
	sudo cp -f $@ /usr/local/sbin

filter_sysname.h: filter_sysname_gen.c sysname.incl
	$(CC) -Wall -O2 -o filter_sysname_gen $<
	@./filter_sysname_gen > $@

filter_db_init: filter_db_init.c filter_sysname.h
	$(CC) -Wall -O2 $(INCLUDES) -o $@ $< $(LIB) -ljson-c

plugin-camera-test-exe: gst_camera sysname.h plugin-libcamera-vid-exe.c mqtt_testlib.c plugin-testlib.c
	$(CC) $(INCLUDES) -Wall -O2 -o $@ $^ $(LIB) -lauparse -laudit -lmosquitto 
	sudo cp -f $@ /usr/local/sbin

clean:
	rm -f plugin-camera-test plugin-camera-test-exe gst_camera subscriber filter sysname.h sysname.incl filter_db_init filter_sysname_gen filter_sysname.h

CXX = gcc
CLIBS = -lxcb -lm -lasound

LIB_DIR = libs
LIBS = $(wildcard $(LIB_DIR)/**/*.c) $(wildcard $(LIB_DIR)/*.c)

SOURCE = audio_visualizer.c


TARGET = audio_visualizer

$(TARGET) : $(LIBS)
	$(CXX) $(CLIBS) -g -o $@ $(SOURCE) $^

clean:
	rm audio_visualizer


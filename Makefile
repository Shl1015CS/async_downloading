CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra
LDFLAGS = -lnghttp2

TARGET = http_downloader
SOURCES = main.cc http_downloader.cc
OBJECTS = $(SOURCES:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean 
# Makefile

# Compiler
CXX = g++

# Compiler Flags
CXXFLAGS = -g -Wall 

# Target executable name
TARGET = p2p_app

# Source files
SRCS = p2p_app.cpp

# Default rule
all: $(TARGET)

# Build rule
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)


# Clean Rule
clean:
	rm -f $(TARGET)
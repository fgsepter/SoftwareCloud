CC=g++
CFLAGS=-c -Wall
LDFLAGS=

COMMON_SOURCES=../common/common_functions.cc
TEST_SOURCES=test.cc kvs.cc
MAIN_SOURCES=backend_main.cc kvs.cc backend_server.cc
MASTER_SOURCES=master_node_main.cc kvs.cc backend_master.cc

TEST_OBJECTS=$(TEST_SOURCES:.cpp=.o) $(COMMON_SOURCES:.cc=.o)
MAIN_OBJECTS=$(MAIN_SOURCES:.cpp=.o) $(COMMON_SOURCES:.cc=.o)
MASTER_OBJECTS=$(MASTER_SOURCES:.cpp=.o) $(COMMON_SOURCES:.cc=.o)

TEST_EXECUTABLE=test
MAIN_EXECUTABLE=main
MASTER_EXECUTABLE=master

all: $(MAIN_EXECUTABLE) $(MASTER_EXECUTABLE) # $(TEST_EXECUTABLE) 


test: $(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_OBJECTS) -lpthread -o $@

$(MAIN_EXECUTABLE): $(MAIN_OBJECTS)
	$(CC) $(LDFLAGS) $(MAIN_OBJECTS) -lpthread -o $@

$(MASTER_EXECUTABLE): $(MASTER_OBJECTS)
	$(CC) $(LDFLAGS) $(MASTER_OBJECTS) -lpthread -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

.cc.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TEST_EXECUTABLE) $(MAIN_EXECUTABLE) $(MASTER_EXECUTABLE) 

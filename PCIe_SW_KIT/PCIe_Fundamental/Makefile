#
TARGET = PCIE_FUNDAMENTAL

#
CFLAGS = -g -Wall
LDFLAGS = -ldl
CC = g++

build: $(TARGET)

$(TARGET): $(TARGET).o PCIE.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) 

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o : %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) *.a *.o *~


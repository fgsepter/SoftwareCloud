TARGETS = httpserver  load_balancer

all: $(TARGETS)

httpserver: httpserver.cc frontend_to_client.cc utils.cc ../common/common_functions.cc
	g++ $^ -lpthread -g -o $@

load_balancer: load_balancer.cc ../common/common_functions.cc
	g++ $^ -lpthread -g -o $@

clean::
	rm -fv $(TARGETS) *~

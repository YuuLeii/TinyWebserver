CXX ?= g++

target = server
src = config.cc http_conn.cc main.cc sql_connection_pool.cc TimerQueue.cc \
        Timestamp.cc webserver.cc

$(target): $(src)
	$(CXX) -o $(OUTPUTDIR)/$@ $^ -lpthread -lmysqlclient -w 

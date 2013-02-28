BIN 	= 	wcamsrv
CFG 	= 	config
CFG_PATH= 	/nfs/rootfs/root/wcamsrv
PREFIX 	= 	/nfs/rootfs/usr/bin
FUNCS 	=

DBG 	=
#DBG     =   -DDBG_APP
#DBG    +=   -DDBG_V4L
#DBG    +=   -DDBG_TCP
#DBG    +=   -DDBG_FBD
#DBG    +=   -DDBG_JPG
#DBG    +=   -DDBG_WCAM
#DBG    +=   -DDBG_VID
#DBG    +=   -DDBG_CFG
#DBG    +=   -DDBG_TPOOL

FUNC 	= 	-DS3C_FB
FUNC   	+= 	-DS3C_JPG
FUNC    += 	-DVID_FUNC

INC 	= 	-Iinclude/
LDFLAGS = 	-lpthread -ljpeg 
SRC 	= 	$(wildcard *.c)
OBJS 	= 	$(patsubst %.c, %.o, $(SRC))
LIBS 	= 	
#CC 	 	= 	gcc
CC 	 	= 	arm-linux-gcc
CFLAGS 	= 	-Wall $(FUNCS) $(INC) $(DBG) $(FUNC)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS) 

clean:
	$(RM) $(OBJS) $(BIN)
install:
#	mkdir -p $(CFG_PATH) && install $(CFG) $(CFG_PATH)
	install $(BIN) $(PREFIX)

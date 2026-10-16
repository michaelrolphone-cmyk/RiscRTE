static const unsigned char bootBytes[]="{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[]}";
static const unsigned char boardBytes[]="{\"schema\":\"riscrte.board-hardware\",\"schema_version\":1,\"board_id\":\"embedded\",\"revision\":\"fixture\",\"buses\":[],\"devices\":[]}";
static const StoreFile embeddedFiles[]={{"/boot.json",bootBytes,sizeof(bootBytes)-1},{"/board.json",boardBytes,sizeof(boardBytes)-1}};

#pragma once
enum {
	MIDB_E_UNKNOWN_COMMAND = 0,
	MIDB_E_PARAMETER_ERROR = 1,
	MIDB_E_HASHTABLE_FULL = 2,
	MIDB_E_NO_FOLDER = 3,
	MIDB_E_NO_MEMORY = 4,
	MIDB_E_NO_MESSAGE = 5,
	MIDB_E_DIGEST = 6,
	MIDB_E_FOLDER_EXISTS = 7,
	MIDB_E_FOLDER_LIMIT = 8,
	MIDB_E_MAILBOX_FULL = 9,
	MIDB_E_NO_DELETE = 10,
	MIDB_E_STORE_NOT_LOADED = 11,
	MIDB_E_STORE_BUSY = 12,
	MIDB_E_NETIO = 13,
	MIDB_E_CREATEFOLDER,
	MIDB_E_DISK_ERROR,
	MIDB_E_IMAIL_DIGEST,
	MIDB_E_IMAIL_RETRIEVE,
	MIDB_E_MDB_ALLOCID,
	MIDB_E_MDB_DELETEFOLDER,
	MIDB_E_MDB_DELETEMESSAGES,
	MIDB_E_MDB_GETFOLDERPROPS,
	MIDB_E_MDB_GETMSGPROPS,
	MIDB_E_MDB_GETSTOREPROPS,
	MIDB_E_MDB_MOVECOPY,
	MIDB_E_MDB_PARTIAL,
	MIDB_E_MDB_SETFOLDERPROPS,
	MIDB_E_MDB_SETMSGPROPS,
	MIDB_E_MDB_SETMSGRD,
	MIDB_E_MDB_WRITEMESSAGE,
	MIDB_E_MNG_CTMATCH,
	MIDB_E_MNG_SORTFOLDER,
	MIDB_E_OXCMAIL_IMPORT,
	MIDB_E_SHORT_READ,
	MIDB_E_SQLPREP,
	MIDB_E_SQLUNEXP,
	MIDB_E_SSGETID,
	MIDB_E_ACCESS_DENIED,
	MIDB_E_NOTPERMITTED,
};

enum midb_flag : char {
	answered = 'A',
	deleted = 'D',
	flagged = 'F',
	recent = 'R',
	seen = 'S',
	unsent = 'U',
	forwarded = 'W',
};

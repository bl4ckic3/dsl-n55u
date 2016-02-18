
typedef enum skip_type {
	SKIP_TYPE_NONE = 0x0,
	SKIP_TYPE_WEATHER = 0x1,
	SKIP_TYPE_WEATHER_BW4080 = 0x2,
	SKIP_TYPE_CE_BW40 = 0x4,
	SKIP_TYPE_CE_BW80 = 0x8,
	SKIP_TYPE_SGAU_BW4080 = 0x10,
}skip_type_t;

typedef struct channel_list {
	int count;
	int list[32];
} channel_list_t;

typedef struct skip_channel_list {
	skip_type_t type;
	channel_list_t channel;
} skip_channel_list_t;

typedef struct region_condition {
	int SupportDFS;
	int UseCEforAPAC;
	int EnableDFSforAPAC;
} region_condition_t; 


int check_UseCEforAPAC(const char *ProductName);
int check_SupportDFS(const char *ProductName, const char *BootLoaderVer);
int check_EnableDFSforAPAC(const char *ProductName, const char *BootLoaderVer);
int check_SupportCountrySelect(const char *ProductName, const char *TerritoryCode);

int getChannelList2G(int region, channel_list_t *channel);
int getCountryRegion2G(const char *countryCode);
int getChannelNumMax2G(int region);

int getChannelList_Skip(skip_type_t type, channel_list_t *list);

int getChannelList5G(int region, channel_list_t *channel, skip_type_t type);
int getCountryRegion5G(const char *countryCode, region_condition_t *condition);
int convert_Channel2str(channel_list_t *channel, const char *delim, char *buf, int buflen);

int get_RegionCode_by_Country(const char *Country, char *buf, int buflen);



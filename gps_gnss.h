int gps_i2c_init(const char* i2c_bus);  // Opens I2C and returns fd
int gps_get_location(int fd, double* latitude, double* longitude); // Gets lat/lon
void gps_i2c_close(int fd); // Close when done
int bq25792_get_charging_status();
int bq25792_get_full_status();
int bq25792_enter_ship_mode() ;
int bq25792_configure_2s_charging();
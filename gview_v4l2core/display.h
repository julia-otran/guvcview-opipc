void start_drm();
void stop_drm();

void init_display(int width, int height, int format);
void terminate_display();
void deallocate_buffers();

int get_buffer_number();
void put_buffer(uint8_t buffer_number);

int get_dma_fd1();
int get_dma_fd2();
int get_dma_fd3();

uint8_t* get_buffer_1();
uint8_t* get_buffer_1();
uint8_t* get_buffer_3();

void get_offsets(uint32_t *u_offset, uint32_t *v_offset);

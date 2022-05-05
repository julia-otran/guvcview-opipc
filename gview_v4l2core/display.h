void init_display(int width, int height);
void terminate_display();

int get_buffer_number();
void put_buffer(uint8_t buffer_number);

int get_dma_fd1();
int get_dma_fd2();
int get_dma_fd3();

void get_offsets(uint32_t *u_offset, uint32_t *v_offset);

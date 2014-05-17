
#define DO16(x) x x x x x x x x x x x x x x x x
#define DO128(x) DO16(x)  DO16(x)  DO16(x)  DO16(x)  DO16(x)  DO16(x)  DO16(x)  DO16(x) 
#define DO1K(x)  DO128(x) DO128(x) DO128(x) DO128(x) DO128(x) DO128(x) DO128(x) DO128(x) 
#define DO8K(x)  DO1K(x)  DO1K(x)  DO1K(x)  DO1K(x)  DO1K(x)  DO1K(x)  DO1K(x)  DO1K(x) 
#define DO64K(x) DO8K(x)  DO8K(x)  DO8K(x)  DO8K(x)  DO8K(x)  DO8K(x)  DO8K(x)  DO8K(x) 


static char pad[64*1024 * 64];

void clearcache()
{
  register char *p = pad;
  DO64K( (*p)++; p+=32; );
  DO64K( (*p)++; p+=32; );
  p++;
}

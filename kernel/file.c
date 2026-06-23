#include "defs.h"
#include "file.h"
#include "param.h"
#include "spinlock.h"

struct devsw devsw[NDEV];

struct {
	struct spinlock lock;
	struct file file[NFILE];
} ftable;

void fileinit(void)
{
	initlock(&ftable.lock, "ftable");
}
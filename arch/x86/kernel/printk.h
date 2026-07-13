#ifndef __PRINTK_H__
#define __PRINTK_H__

/*
 * 内核日志级别（数值越小越紧急，与 Linux 一致）。
 * printk(KERN_INFO "msg %d\n", x);  — 级别嵌在格式串前缀里。
 */
#define LOGLEVEL_EMERG		0	/* 系统不可用 */
#define LOGLEVEL_ALERT		1	/* 必须立即处理 */
#define LOGLEVEL_CRIT		2	/* 严重错误 */
#define LOGLEVEL_ERR		3	/* 错误 */
#define LOGLEVEL_WARNING	4	/* 警告 */
#define LOGLEVEL_NOTICE		5	/* 正常但重要 */
#define LOGLEVEL_INFO		6	/* 信息 */
#define LOGLEVEL_DEBUG		7	/* 调试 */

/* 经典 Linux 前缀：嵌入格式串开头，printk 解析后剥掉 */
#define KERN_EMERG	"<0>"
#define KERN_ALERT	"<1>"
#define KERN_CRIT	"<2>"
#define KERN_ERR	"<3>"
#define KERN_WARNING	"<4>"
#define KERN_NOTICE	"<5>"
#define KERN_INFO	"<6>"
#define KERN_DEBUG	"<7>"
#define KERN_DEFAULT	""		/* 无前缀时用默认级别 INFO */

/* 高于此级别的消息不打印到控制台（默认 INFO → DEBUG 被过滤） */
extern int console_loglevel;

int printk(const char *fmt, ...);

#endif

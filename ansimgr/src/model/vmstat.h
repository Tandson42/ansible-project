#ifndef ANSIMGR_VMSTAT_H
#define ANSIMGR_VMSTAT_H

#define PORTMAX 8

typedef struct {
    char name[32];
    int  port;
    int  listening;
} PortProbe;

typedef struct {
    int  qemu_pid;         /* pid of a qemu process using this project's disk, 0 if none */
    int  ssh_listening;    /* 2222 */
    int  http_listening;   /* 8080 */
    char ssh_host[128];
    char ssh_user[128];
    long uptime_s;         /* seconds since the qemu process started, -1 if unknown */
    long checked_at;       /* monotonic seconds of the last probe */
} VmStat;

/* Reads /proc directly: no dependency on ss, netstat or nc. */
void vmstat_probe(VmStat *vs, const char *disk_path, const char *inv_path);
int  port_is_listening(int port);
const char *vmstat_summary(const VmStat *vs);

#endif /* ANSIMGR_VMSTAT_H */

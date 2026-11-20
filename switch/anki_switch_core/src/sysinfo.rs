//! The memory this process has and uses, for RWKV's checks and logs: on the
//! console from Horizon (svcGetInfo), on Linux (the desktop preview and the
//! host tests) from /proc.

/// Memory of this process, in bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Memory {
    /// What the process may use: on the console about 3.2 GB when started
    /// from a game (full memory), far less from the album (applet mode).
    pub total: u64,
    pub used: u64,
}

impl Memory {
    pub fn free(&self) -> u64 {
        self.total.saturating_sub(self.used)
    }
}

impl std::fmt::Display for Memory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} MB used of {} MB ({} MB free)", mb(self.used), mb(self.total), mb(self.free()))
    }
}

pub fn mb(bytes: u64) -> u64 {
    (bytes + (1 << 19)) >> 20
}

/// On the console: the malloc heap. libnx reserves (nearly) all the
/// process's memory for it at startup, so Horizon's own counters
/// (svcGetInfo: TotalMemorySize, UsedMemorySize) always show it full; what is
/// free is the heap's size minus what malloc has handed out (newlib's
/// mallinfo).
#[cfg(target_os = "horizon")]
pub fn memory() -> Option<Memory> {
    #[repr(C)]
    struct MallInfo {
        arena: usize,
        ordblks: usize,
        smblks: usize,
        hblks: usize,
        hblkhd: usize,
        usmblks: usize,
        fsmblks: usize,
        uordblks: usize,
        fordblks: usize,
        keepcost: usize,
    }
    extern "C" {
        // newlib (devkitA64): where libnx's heap starts and ends
        static fake_heap_start: *mut u8;
        static fake_heap_end: *mut u8;
        fn mallinfo() -> MallInfo;
    }
    let (start, end) = unsafe { (fake_heap_start as usize, fake_heap_end as usize) };
    let total = end.checked_sub(start).filter(|&n| n > 0)? as u64;
    let info = unsafe { mallinfo() };
    Some(Memory { total, used: (info.uordblks + info.hblkhd) as u64 })
}

/// Horizon's view of the process (total and used, bytes), heap included:
/// logged next to `memory()` for reference.
#[cfg(target_os = "horizon")]
pub fn process_memory() -> Option<Memory> {
    extern "C" {
        // libnx (svc.s): Result svcGetInfo(u64 *out, u32 id0, Handle handle, u64 id1)
        fn svcGetInfo(out: *mut u64, id0: u32, handle: u32, id1: u64) -> u32;
    }
    const TOTAL_MEMORY_SIZE: u32 = 6; // InfoType_TotalMemorySize
    const USED_MEMORY_SIZE: u32 = 7; // InfoType_UsedMemorySize
    const CUR_PROCESS_HANDLE: u32 = 0xFFFF8001;
    let get = |id| {
        let mut out = 0u64;
        (unsafe { svcGetInfo(&mut out, id, CUR_PROCESS_HANDLE, 0) } == 0).then_some(out)
    };
    Some(Memory { total: get(TOTAL_MEMORY_SIZE)?, used: get(USED_MEMORY_SIZE)? })
}

#[cfg(not(target_os = "horizon"))]
pub fn process_memory() -> Option<Memory> {
    memory()
}

/// For logs: "120 MB used of 3000 MB (2880 MB free)", or "unknown".
pub fn memory_text() -> String {
    memory().map_or("unknown".into(), |m| m.to_string())
}

#[cfg(not(target_os = "horizon"))]
pub fn memory() -> Option<Memory> {
    // (kB lines: "VmRSS:   123456 kB", "MemAvailable:  7890 kB")
    let field = |text: &str, name: &str| -> Option<u64> {
        let line = text.lines().find(|l| l.starts_with(name))?;
        line[name.len()..].trim().trim_end_matches("kB").trim().parse::<u64>().ok().map(|kb| kb * 1024)
    };
    let used = field(&std::fs::read_to_string("/proc/self/status").ok()?, "VmRSS:")?;
    let available = field(&std::fs::read_to_string("/proc/meminfo").ok()?, "MemAvailable:")?;
    Some(Memory { total: used + available, used })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn memory_is_read_and_shown() {
        let m = memory().expect("readable on Linux");
        assert!(m.used > 0 && m.total > m.used, "{m:?}");
        let m = Memory { total: 3 << 30, used: 512 << 20 };
        assert_eq!(m.to_string(), "512 MB used of 3072 MB (2560 MB free)");
    }
}

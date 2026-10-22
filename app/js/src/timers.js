// Timers and tasks. The app runs the loop: after each entry into the card it
// asks nextTimer() and calls runTimers() when that time has come (see
// CardScript in app/src/script/card_script.cpp). Microtasks (promises) are
// run by QuickJS's job queue.
import { reportError } from "./errors.js";

const timers = new Map(); // id -> {callback, args, due, interval, order}
let lastId = 0;
let order = 0;

const now = () => performance.now();

function add(callback, delay, args, repeat) {
    delay = Math.max(0, Number(delay) || 0);
    const id = ++lastId;
    timers.set(id, { callback, args, due: now() + delay, interval: repeat ? Math.max(1, delay) : -1, order: ++order });
    return id;
}

function handler(callback) {
    // setTimeout("code", ms) runs the string as a script.
    return typeof callback === "function" ? callback : () => (0, eval)(String(callback));
}

export function setTimeout(callback, delay, ...args) { return add(handler(callback), delay, args, false); }
export function setInterval(callback, delay, ...args) { return add(handler(callback), delay, args, true); }
export function clearTimeout(id) { timers.delete(Number(id)); }
export const clearInterval = clearTimeout;

// A task for the runtime itself (script loads, events fired later).
export function queueTask(fn) { add(fn, 0, [], false); }

export function requestAnimationFrame(callback) {
    return add(() => callback(now()), 16, [], false);
}
export const cancelAnimationFrame = clearTimeout;

export function requestIdleCallback(callback) {
    const deadline = { didTimeout: false, timeRemaining: () => 50 };
    return add(() => callback(deadline), 1, [], false);
}
export const cancelIdleCallback = clearTimeout;

// Milliseconds until the next timer is due (0: now), or -1 if there is none.
export function nextTimer() {
    let first = Infinity;
    for (const t of timers.values()) first = Math.min(first, t.due);
    return first === Infinity ? -1 : Math.max(0, Math.ceil(first - now()));
}

// Run the timers that are due, earliest first. Ones they add wait for the
// next call, as in browsers (a timer never runs within the task that set it).
export function runTimers() {
    const t0 = now();
    const due = [...timers].filter(([, t]) => t.due <= t0).sort(([, a], [, b]) => a.due - b.due || a.order - b.order);
    for (const [id, t] of due) {
        if (timers.get(id) !== t) continue; // cleared by an earlier one
        if (t.interval < 0) timers.delete(id);
        else t.due = now() + t.interval;
        try {
            t.callback(...t.args);
        } catch (e) {
            reportError(e);
        }
    }
}

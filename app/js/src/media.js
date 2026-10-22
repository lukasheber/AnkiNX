// The card page's address and the media folder behind it. Anki desktop and
// AnkiDroid serve the collection's media at the root of a local web server,
// so "_lib.js", "./img.png" and "/img.png" are media files; nothing else is
// reachable (cards run offline).
import { URL } from "./url.js";

export const PAGE_URL = "http://127.0.0.1/";
const ORIGIN = "http://127.0.0.1";

// The media file `url` names, or null.
export function mediaPath(url) {
    let u;
    try {
        u = new URL(String(url), PAGE_URL);
    } catch (e) {
        return null;
    }
    if (u.origin !== ORIGIN) return null;
    let path;
    try {
        path = decodeURIComponent(u.pathname.slice(1));
    } catch (e) {
        return null;
    }
    if (!path || path.split("/").some(part => part === ".." || part === "")) return null;
    return path;
}

// Libraries the page already has: a template that loads its own copy from a
// CDN (no network on the console) gets the bundled one.
export function bundledLibrary(url) {
    const u = String(url).toLowerCase();
    if (/(^|[/.-])jquery([.-][\d.]+)?(\.slim)?(\.min)?\.js(\?|$)/.test(u)) return "jquery";
    return null;
}

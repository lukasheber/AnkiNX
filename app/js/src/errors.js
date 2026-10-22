// Uncaught errors in card scripts: logged, and reported to the page as an
// "error" event on window (window.onerror), as browsers do. They never stop
// the card.
import { native } from "./native.js";
import { ErrorEvent, dispatchEvent, setErrorReporter } from "./events.js";

let reporting = false;

export function describe(error) {
    if (error instanceof Error) return error.stack ? `${error}\n${error.stack}` : String(error);
    try {
        return typeof error === "string" ? error : JSON.stringify(error);
    } catch (e) {
        return String(error);
    }
}

export function reportError(error, filename = "") {
    native.log(2, describe(error));
    if (reporting) return; // an error in an error handler
    reporting = true;
    try {
        const message = error instanceof Error ? `Uncaught ${error.name}: ${error.message}` : `Uncaught ${String(error)}`;
        dispatchEvent(globalThis, new ErrorEvent("error", { cancelable: true, message, filename, error }));
    } finally {
        reporting = false;
    }
}

setErrorReporter(reportError);

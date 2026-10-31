// The app's side of the bridge, installed by app/src/script/card_script.cpp
// as __native before this bundle runs. Taken off the global object so card
// scripts only see the browser APIs.
//
//   log(level, message)              0 info, 1 warning, 2 error
//   evalScript(code, name)           a classic script, in the global scope
//   evalModule(code, name)           a module script (imports from the media folder)
//   readMedia(path)                  a media file as text, or null
//   readMediaBytes(path)             a media file as an ArrayBuffer, or null
//   storage(kind, op, key, value)    localStorage / sessionStorage, kept for the app session
//   command(text)                    to the reviewer: "ans", "ease1".."ease4", "link:<url>", ...
//   measure(html)                    layout of the body HTML: {serial: [x, y, w, h,
//                                    clientLeft, clientTop, clientWidth, clientHeight, style]}, or null
//   viewport()                       [width, height, scrollY, devicePixelRatio] in CSS pixels
//   scrollTo(y)                      scroll the card (CSS pixels)
//   schedulingStates()               the shown card's scheduling states and context as JSON
//                                    ({states, context}, see scheduling.js), or null
//   setSchedulingStates(key, json)   states for answering the card, from custom scheduling code
export const native = globalThis.__native;
delete globalThis.__native;

// Commands sent so far (a tap that sends one was handled by the card).
export const stats = { commands: 0 };
const command = native.command;
native.command = text => {
    stats.commands++;
    command(text);
};

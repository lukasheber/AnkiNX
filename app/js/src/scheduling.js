// Custom scheduling, ported from Anki's ts/reviewer/answering.ts (Anki
// 26.09): anki.mutateNextCardStates(key, transform) runs the collection's
// code (deck options > Advanced > Custom scheduling) on the shown card's
// scheduling states, as JSON, with each answer button's custom data and the
// scheduling context, then hands back what it left, with the key the
// reviewer gave the code (states handed back with another key are ignored,
// as in Anki).
//
// Anki's page holds the states as protobuf-es 1.10 messages: it gives the
// code toJson({emitDefaultValues: true}) of them and reads the result with
// fromJson(), which throws on what doesn't fit the schema (an unknown key,
// two kinds at once, a fraction or undefined where whole seconds go...), so
// the code's change is dropped. Both are ported below for the messages
// under SchedulingStates; the backend sends and takes the same JSON
// (switch/anki_switch_core/src/scheduling.rs).
import { native } from "./native.js";
import { describe } from "./errors.js";

// ---------------------------------------- anki/scheduler.proto, as JSON --

// The messages under SchedulingStates, fields in number order: [JSON name,
// type, "kind" for the members of the message's oneof]. Types are uint32,
// float, bool, "optional string", or a message here (which can be unset).
const SCHEMA = {
    "anki.scheduler.SchedulingStates": [
        ["current", "anki.scheduler.SchedulingState"],
        ["again", "anki.scheduler.SchedulingState"],
        ["hard", "anki.scheduler.SchedulingState"],
        ["good", "anki.scheduler.SchedulingState"],
        ["easy", "anki.scheduler.SchedulingState"],
    ],
    "anki.scheduler.SchedulingState": [
        ["normal", "anki.scheduler.SchedulingState.Normal", "kind"],
        ["filtered", "anki.scheduler.SchedulingState.Filtered", "kind"],
        ["customData", "optional string"],
    ],
    "anki.scheduler.SchedulingState.Normal": [
        ["new", "anki.scheduler.SchedulingState.New", "kind"],
        ["learning", "anki.scheduler.SchedulingState.Learning", "kind"],
        ["review", "anki.scheduler.SchedulingState.Review", "kind"],
        ["relearning", "anki.scheduler.SchedulingState.Relearning", "kind"],
    ],
    "anki.scheduler.SchedulingState.New": [["position", "uint32"]],
    "anki.scheduler.SchedulingState.Learning": [
        ["remainingSteps", "uint32"],
        ["scheduledSecs", "uint32"],
        ["elapsedSecs", "uint32"],
        ["memoryState", "anki.cards.FsrsMemoryState"],
    ],
    "anki.scheduler.SchedulingState.Review": [
        ["scheduledDays", "uint32"],
        ["elapsedDays", "uint32"],
        ["easeFactor", "float"],
        ["lapses", "uint32"],
        ["leeched", "bool"],
        ["memoryState", "anki.cards.FsrsMemoryState"],
    ],
    "anki.scheduler.SchedulingState.Relearning": [
        ["review", "anki.scheduler.SchedulingState.Review"],
        ["learning", "anki.scheduler.SchedulingState.Learning"],
    ],
    "anki.scheduler.SchedulingState.Filtered": [
        ["preview", "anki.scheduler.SchedulingState.Preview", "kind"],
        ["rescheduling", "anki.scheduler.SchedulingState.ReschedulingFilter", "kind"],
    ],
    "anki.scheduler.SchedulingState.Preview": [
        ["scheduledSecs", "uint32"],
        ["finished", "bool"],
    ],
    "anki.scheduler.SchedulingState.ReschedulingFilter": [["originalState", "anki.scheduler.SchedulingState.Normal"]],
    "anki.cards.FsrsMemoryState": [
        ["stability", "float"],
        ["difficulty", "float"],
    ],
};
const STATES = "anki.scheduler.SchedulingStates";

// Fields by JSON name and by proto name (fromJson takes either).
const FIELDS = {};
for (const [type, fields] of Object.entries(SCHEMA)) {
    FIELDS[type] = new Map();
    for (const [json, kind, oneof] of fields) {
        const field = { json, name: json.replace(/[A-Z]/g, c => "_" + c.toLowerCase()), type: kind, oneof };
        FIELDS[type].set(field.json, field).set(field.name, field);
    }
}
const ZERO = { uint32: 0, float: 0, bool: false };
const UINT32_MAX = 0xffffffff;
const FLOAT32_MAX = 3.4028234663852886e38;

// A message is a plain object: its scalars always there (zero by default),
// the rest only when set; at most one member of a oneof.
function newMessage(type) {
    const message = {};
    for (const [json, kind] of SCHEMA[type]) if (kind in ZERO) message[json] = ZERO[kind];
    return message;
}

// What a value is called in an error (protobuf-es debugJsonValue).
function debugJson(json) {
    if (json === null) return "null";
    switch (typeof json) {
        case "object":
            return Array.isArray(json) ? "array" : "object";
        case "string":
            return json.length > 100 ? "string" : `"${json.split('"').join('\\"')}"`;
        default:
            return String(json);
    }
}

// fromJson(): protobuf-es readMessage.
function readMessage(type, json) {
    if (json == null || Array.isArray(json) || typeof json != "object")
        throw new Error(`cannot decode message ${type} from JSON: ${debugJson(json)}`);
    const message = newMessage(type);
    let oneofSeen;
    for (const [jsonKey, jsonValue] of Object.entries(json)) {
        const field = FIELDS[type].get(jsonKey);
        if (!field) throw new Error(`cannot decode message ${type} from JSON: key "${jsonKey}" is unknown`);
        if (field.oneof) {
            if (oneofSeen !== undefined)
                throw new Error(`cannot decode message ${type} from JSON: multiple keys for oneof "${field.oneof}" present: "${oneofSeen}", "${jsonKey}"`);
            oneofSeen = jsonKey;
        }
        readField(message, jsonValue, field, type);
    }
    return message;
}

// protobuf-es readField, for the field kinds here.
function readField(message, jsonValue, field, type) {
    if (field.type in SCHEMA) {
        if (jsonValue === null) {
            // A oneof's member: protobuf-es sets the kind without a value,
            // which fails when Anki sends the states on (the change is
            // dropped). Here it fails at once.
            if (field.oneof) throw new Error(`cannot decode field ${type}.${field.name} from JSON: null`);
            return; // an unset message
        }
        message[field.json] = readMessage(field.type, jsonValue);
        return;
    }
    try {
        if (jsonValue === null) {
            // Back to its default (an optional field: unset).
            if (field.type in ZERO) message[field.json] = ZERO[field.type];
            else delete message[field.json];
            return;
        }
        message[field.json] = readScalar(field.type, jsonValue);
    } catch (e) {
        let m = `cannot decode field ${type}.${field.name} from JSON: ${debugJson(jsonValue)}`;
        if (e instanceof Error && e.message.length > 0) m += `: ${e.message}`;
        throw new Error(m);
    }
}

// protobuf-es readScalar: a number or a numeric string for numbers (whole
// and in range for uint32; "NaN", "Infinity" and "-Infinity" for floats).
function readScalar(type, json) {
    switch (type) {
        case "float": {
            if (json === "NaN") return Number.NaN;
            if (json === "Infinity") return Number.POSITIVE_INFINITY;
            if (json === "-Infinity") return Number.NEGATIVE_INFINITY;
            if (json === "") break;
            if (typeof json == "string" && json.trim().length !== json.length) break;
            if (typeof json != "string" && typeof json != "number") break;
            const float = Number(json);
            if (Number.isNaN(float) || !Number.isFinite(float)) break;
            if (float > FLOAT32_MAX || float < -FLOAT32_MAX) throw new Error("invalid float 32: " + float);
            return float;
        }
        case "uint32": {
            let int32;
            if (typeof json == "number") int32 = json;
            else if (typeof json == "string" && json.length > 0 && json.trim().length === json.length) int32 = Number(json);
            if (int32 === undefined) break;
            if (!Number.isInteger(int32) || int32 > UINT32_MAX || int32 < 0) throw new Error("invalid uint 32: " + int32);
            return int32;
        }
        case "bool":
            if (typeof json !== "boolean") break;
            return json;
        case "optional string":
            if (typeof json !== "string") break;
            try {
                encodeURIComponent(json);
            } catch (e) {
                throw new Error("invalid UTF8");
            }
            return json;
    }
    throw new Error();
}

// toJson({emitDefaultValues: true}): protobuf-es writeMessage, fields in
// number order (as the code sees them, e.g. in JSON.stringify(states)).
function writeMessage(type, message) {
    const json = {};
    for (const [key, kind] of SCHEMA[type]) {
        const value = message[key];
        if (value === undefined) continue;
        json[key] = kind in SCHEMA ? writeMessage(kind, value) : writeScalar(kind, value);
    }
    return json;
}

function writeScalar(type, value) {
    if (type === "float") {
        if (Number.isNaN(value)) return "NaN";
        if (value === Number.POSITIVE_INFINITY) return "Infinity";
        if (value === Number.NEGATIVE_INFINITY) return "-Infinity";
    }
    return value;
}

// The card's deck name, its review seed (a uint64: a BigInt, as protobuf-es
// has it), and its FSRS decay and desired retention when it has them.
class SchedulingContext {
    constructor(json) {
        this.deckName = json.deckName ?? "";
        this.seed = BigInt(json.seed ?? 0);
        this.decay = json.decay === undefined ? undefined : readScalar("float", json.decay);
        this.desiredRetention = json.desiredRetention === undefined ? undefined : readScalar("float", json.desiredRetention);
    }

    toJson(options = {}) {
        const json = {};
        if (options.emitDefaultValues || this.deckName !== "") json.deckName = this.deckName;
        if (options.emitDefaultValues || this.seed !== 0n) json.seed = this.seed.toString();
        if (this.decay !== undefined) json.decay = writeScalar("float", this.decay);
        if (this.desiredRetention !== undefined) json.desiredRetention = writeScalar("float", this.desiredRetention);
        return json;
    }

    toJsonString(options = {}) {
        return JSON.stringify(this.toJson(options), null, options.prettySpaces ?? 0);
    }

    // JSON.stringify(ctx), as protobuf-es messages do.
    toJSON() {
        return this.toJson({ emitDefaultValues: true });
    }
}

// ------------------------------------------- ts/reviewer/answering.ts --

function unpackCustomData(states) {
    const toObject = s => {
        try {
            return JSON.parse(s);
        } catch {
            return {};
        }
    };
    return {
        again: toObject(states.current.customData),
        hard: toObject(states.current.customData),
        good: toObject(states.current.customData),
        easy: toObject(states.current.customData),
    };
}

function packCustomData(states, customData) {
    states.again.customData = JSON.stringify(customData.again);
    states.hard.customData = JSON.stringify(customData.hard);
    states.good.customData = JSON.stringify(customData.good);
    states.easy.customData = JSON.stringify(customData.easy);
}

export async function mutateNextCardStates(key, transform) {
    try {
        const statesWithContext = await getSchedulingStatesWithContext();
        const updatedStates = await applyStateTransform(statesWithContext, transform);
        await setSchedulingStates({ key, states: updatedStates });
    } catch (error) {
        // What a browser's console shows of the rejection.
        native.log(2, "custom scheduling: " + describe(error));
        throw error;
    }
}

async function applyStateTransform(states, transform) {
    // convert to JSON, which is the format existing transforms expect
    const statesJson = writeMessage(STATES, states.states);

    // decode customData and put it into each state
    const customData = unpackCustomData(states.states);

    // run the user function on the JSON
    await transform(statesJson, customData, states.context);

    // convert the JSON back into proto form, and pack the custom data in
    const updatedStates = readMessage(STATES, statesJson);
    packCustomData(updatedStates, customData);

    return updatedStates;
}

// The backend's side (Anki's @generated/backend calls), through the app.
async function getSchedulingStatesWithContext() {
    const text = native.schedulingStates();
    if (text === null) throw new Error("no card is being studied");
    const json = JSON.parse(text);
    return { states: readMessage(STATES, json.states), context: new SchedulingContext(json.context) };
}

async function setSchedulingStates({ key, states }) {
    native.setSchedulingStates(String(key), JSON.stringify(writeMessage(STATES, states)));
}

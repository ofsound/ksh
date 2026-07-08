import { getNativeFunction } from "@juce";

export function isBackendReady() {
  const functions = window.__JUCE__?.initialisationData?.__juce__functions;
  return Array.isArray(functions) && functions.includes("kshSendCommand");
}

export function nativeFunctionAvailable(name) {
  const functions = window.__JUCE__?.initialisationData?.__juce__functions;
  return Array.isArray(functions) && functions.includes(name);
}

/** Resolve when the plugin WebView has registered native functions. */
export function waitForBackend(timeoutMs = 15000) {
  if (isBackendReady()) {
    return Promise.resolve();
  }

  return new Promise((resolve, reject) => {
    const started = Date.now();

    const check = () => {
      if (isBackendReady()) {
        resolve();
        return;
      }

      if (Date.now() - started > timeoutMs) {
        reject(
          new Error(
            "JUCE native bridge not ready. Debug builds need `npm run dev` in ui/ (localhost:5173)."
          )
        );
        return;
      }

      window.requestAnimationFrame(check);
    };

    check();
  });
}

function getSendCommandNative() {
  return getNativeFunction("kshSendCommand");
}

/** Serializes engine commands so native playback table rebuilds never overlap. */
let commandChain = Promise.resolve();

export function sendCommand(selector, args = []) {
  const run = () => getSendCommandNative()(JSON.stringify({ selector, args }));
  const task = commandChain.then(run, run);
  commandChain = task.catch(() => {});
  return task;
}

export function onBackendEvent(eventId, callback) {
  if (!window.__JUCE__?.backend) {
    return () => {};
  }

  return window.__JUCE__.backend.addEventListener(eventId, callback);
}

export function syncAll() {
  return sendCommand("sync_all");
}

export function setViewSize(width, height, patternViewScale) {
  if (!nativeFunctionAvailable("kshSetViewSize")) {
    return Promise.resolve(false);
  }

  const native = getNativeFunction("kshSetViewSize");
  if (patternViewScale === undefined) {
    return native(width, height);
  }

  return native(width, height, patternViewScale);
}

export function getProjectState() {
  if (!nativeFunctionAvailable("kshGetProjectState")) {
    return Promise.resolve(null);
  }

  return getNativeFunction("kshGetProjectState")();
}

export function newProject() {
  if (!nativeFunctionAvailable("kshNewProject")) {
    return Promise.resolve({ success: 0, error: "Project files are not available." });
  }

  return getNativeFunction("kshNewProject")();
}

export function loadProject() {
  if (!nativeFunctionAvailable("kshLoadProject")) {
    return Promise.resolve({ success: 0, error: "Project files are not available." });
  }

  return getNativeFunction("kshLoadProject")();
}

export function saveProject(name, description) {
  if (!nativeFunctionAvailable("kshSaveProject")) {
    return Promise.resolve({ success: 0, error: "Project files are not available." });
  }

  return getNativeFunction("kshSaveProject")(name, description);
}

export function cycleProject(direction) {
  if (!nativeFunctionAvailable("kshCycleProject")) {
    return Promise.resolve({ success: 0, error: "Project files are not available." });
  }

  return getNativeFunction("kshCycleProject")(direction);
}

/** Backend may emit parsed objects or { json: string } fallback. */
export function parseBackendJson(payload) {
  if (payload == null) {
    return null;
  }

  if (typeof payload === "string") {
    return JSON.parse(payload);
  }

  if (typeof payload.json === "string") {
    return JSON.parse(payload.json);
  }

  return payload;
}

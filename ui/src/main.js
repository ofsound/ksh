import "./app.css";
import { mount } from "svelte";
import App from "./App.svelte";

function isEditableTarget(target) {
  return (
    target instanceof HTMLInputElement ||
    target instanceof HTMLTextAreaElement ||
    target?.isContentEditable
  );
}

document.addEventListener(
  "selectstart",
  (event) => {
    if (!isEditableTarget(event.target)) {
      event.preventDefault();
    }
  },
  true
);

document.addEventListener(
  "keydown",
  (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "a") {
      if (!isEditableTarget(event.target)) {
        event.preventDefault();
      }
    }
  },
  true
);

mount(App, { target: document.getElementById("app") });

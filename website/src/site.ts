// The shared page chrome: over the home hero the header turns opaque once the
// page scrolls, and on narrow screens the menu opens as a side panel.

const root = document.documentElement;
const header = document.querySelector<HTMLElement>("[data-header]");
const hero = document.querySelector<HTMLElement>(".hero");

if (header && hero) {
    root.classList.add("has-hero");
    const update = () => {
        header.classList.toggle("is-opaque", window.scrollY > hero.offsetHeight - header.offsetHeight - 1);
    };
    window.addEventListener("scroll", update, { passive: true });
    window.addEventListener("resize", update);
    update();
}

const toggle = document.querySelector<HTMLButtonElement>("[data-nav-toggle]");

function setMenu(open: boolean) {
    root.classList.toggle("nav-open", open);
    toggle?.setAttribute("aria-expanded", String(open));
    toggle?.setAttribute("aria-label", open ? "Close the menu" : "Open the menu");
}

toggle?.addEventListener("click", () => setMenu(!root.classList.contains("nav-open")));
document.querySelector("[data-nav-close]")?.addEventListener("click", () => setMenu(false));
document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && root.classList.contains("nav-open")) {
        setMenu(false);
        toggle?.focus();
    }
});
window.matchMedia("(min-width: 48rem)").addEventListener("change", (query) => {
    if (query.matches) setMenu(false);
});

export {};

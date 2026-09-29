// The shared docs chrome: the header nav, the active-page marker and a
// client-side section search.

const PAGES = [
    { t: "Getting started", p: "getting-started.html" },
    { t: "Language", p: "language.html" },
    { t: "Format", p: "format.html" },
    { t: "Backends", p: "backends.html" },
    { t: "FAQ", p: "faq.html" },
];

// Every searchable section: a title, its page and its anchor.
const SECTIONS = [
    { t: "Getting started — install the toolchain", p: "getting-started.html", a: "install" },
    { t: "Getting started — your first asset", p: "getting-started.html", a: "first-asset" },
    { t: "Getting started — the editor", p: "getting-started.html", a: "editor" },
    { t: "Language — the asset block", p: "language.html", a: "asset" },
    { t: "Language — inputs and outputs", p: "language.html", a: "io" },
    { t: "Language — statements", p: "language.html", a: "statements" },
    { t: "Language — expressions and operators", p: "language.html", a: "expressions" },
    { t: "Language — builtins", p: "language.html", a: "builtins" },
    { t: "Language — parts and composition", p: "language.html", a: "parts" },
    { t: "Format — the file layout", p: "format.html", a: "layout" },
    { t: "Format — the sections", p: "format.html", a: "sections" },
    { t: "Format — determinism", p: "format.html", a: "determinism" },
    { t: "Backends — the CLI toolchain", p: "backends.html", a: "cli" },
    { t: "Backends — HTML5 / WASM", p: "backends.html", a: "html5" },
    { t: "Backends — VS Code", p: "backends.html", a: "vscode" },
    { t: "Backends — native hosts (planned)", p: "backends.html", a: "native" },
    { t: "FAQ — determinism", p: "faq.html", a: "determinism" },
    { t: "FAQ — seed, variant, archetype", p: "faq.html", a: "seed" },
    { t: "FAQ — why a VM", p: "faq.html", a: "vm" },
];

function renderChrome() {
    const current = location.pathname.split("/").pop() || "getting-started.html";
    const nav = document.querySelector("header nav");
    for (const page of PAGES) {
        const link = document.createElement("a");
        link.href = page.p;
        link.textContent = page.t;
        if (page.p === current) link.className = "active";
        nav.appendChild(link);
    }

    const results = document.getElementById("results");
    const search = document.getElementById("search");
    search.addEventListener("input", () => {
        const query = search.value.trim().toLowerCase();
        results.textContent = "";
        if (query.length < 2) { results.style.display = "none"; return; }
        const matches = SECTIONS.filter((s) => s.t.toLowerCase().includes(query)).slice(0, 8);
        for (const section of matches) {
            const link = document.createElement("a");
            link.href = section.p + "#" + section.a;
            link.textContent = section.t;
            results.appendChild(link);
        }
        results.style.display = matches.length ? "block" : "none";
    });
    document.addEventListener("click", (event) => {
        if (!event.target.closest("#search-wrap")) results.style.display = "none";
    });
}

document.addEventListener("DOMContentLoaded", renderChrome);

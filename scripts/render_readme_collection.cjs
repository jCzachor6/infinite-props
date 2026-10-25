#!/usr/bin/env node
"use strict";

// Reproduce the README's collector display from the shipped website worker.
// Requires Node 22+ and build/wasm/{iprop.js,iprop.wasm}.
// Add --png to export the README and website social images with puppeteer-core:
// IPROP_BROWSER=/path/to/chromium node scripts/render_readme_collection.cjs --png
// IPROP_PUPPETEER may point to a puppeteer-core installation outside this repo.
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const zlib = require("node:zlib");
const crypto = require("node:crypto");
const {pathToFileURL} = require("node:url");

const root = path.resolve(__dirname, "..");
const args = process.argv.slice(2);
const wasm = path.resolve(args.find(arg => !arg.startsWith("--")) || path.join(root, "build/wasm"));
const output = path.join(root, "examples/showcase/readme-collection.svg");
const digest = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
const seeds = [7, 26, 42, 101, 256, 512, 1024, 5555, 2026, 4096, 54321, 16384, 32768, 65535, 131072, 4294967295];

function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
        crc ^= byte;
        for (let i = 0; i < 8; ++i) crc = crc >>> 1 ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}
function png(part) {
    const chunk = (type, data) => {
        const body = Buffer.concat([Buffer.from(type), data]);
        const size = Buffer.alloc(4), crc = Buffer.alloc(4);
        size.writeUInt32BE(data.length); crc.writeUInt32BE(crc32(body));
        return Buffer.concat([size, body, crc]);
    };
    const header = Buffer.alloc(13);
    header.writeUInt32BE(part.width, 0); header.writeUInt32BE(part.height, 4);
    header[8] = 8; header[9] = 6;
    const rows = Buffer.alloc((part.width * 4 + 1) * part.height);
    for (let y = 0; y < part.height; ++y)
        Buffer.from(part.data.buffer, part.data.byteOffset + y * part.width * 4, part.width * 4)
            .copy(rows, y * (part.width * 4 + 1) + 1);
    return "data:image/png;base64," + Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
        chunk("IHDR", header), chunk("IDAT", zlib.deflateSync(rows)), chunk("IEND", Buffer.alloc(0))]).toString("base64");
}

async function generate() {
    let reply;
    const context = vm.createContext({
        URL, Response, WebAssembly, TextDecoder, TextEncoder, performance, console,
        setTimeout, clearTimeout, crypto:crypto.webcrypto,
        WorkerGlobalScope:function WorkerGlobalScope() {},
        location:{href:"https://readme.invalid/butterfly-worker.js"},
        fetch:async url => {
            const name = new URL(url).pathname.slice(1);
            const file = name === "iprop.wasm" ? path.join(wasm, name)
                : /^showcase\/butterfly-[a-z]+\.(iprop|json)$/.test(name)
                    ? path.join(root, "examples", name) : null;
            if (!file) throw new Error("Unexpected worker resource: " + name);
            return new Response(fs.readFileSync(file), {headers:{"Content-Type":name.endsWith(".wasm")
                ? "application/wasm" : "application/octet-stream"}});
        },
        postMessage:(message,transfer=[]) => { reply = structuredClone(message,{transfer}); },
    });
    context.self = context;
    context.importScripts = url => {
        if (new URL(url).pathname !== "/iprop.js") throw new Error("Unexpected worker script");
        vm.runInContext(fs.readFileSync(path.join(wasm,"iprop.js"),"utf8"),context);
    };
    const worker = fs.readFileSync(path.join(root,"website/public/butterfly-worker.js"),"utf8");
    vm.runInContext(worker,context);
    await context.onmessage({data:{type:"init"}});
    if (reply.type !== "ready") throw new Error(reply.message);
    const frames = [];
    for (const seed of seeds) {
        await context.onmessage({data:{type:"generate",id:frames.length+1,seed}});
        if (reply.type !== "generated") throw new Error(reply.message);
        frames.push(reply);
    }
    return {frames,worker};
}

// This is the resting snapshot assembly from website/src/butterfly.ts:
// hindwings first, mirrored forewings second, then the cropped mirrored body.
function specimen(frame) {
    const images = [];
    for (const name of ["hindwing","forewing"]) {
        const part = frame.parts[name], href = png(part);
        if (part.width !== 128 || part.height !== 256) throw new Error("Unexpected wing dimensions");
        images.push(`<image href="${href}" width="128" height="256" transform="translate(128 0) scale(-1 1)"/>`);
        images.push(`<image href="${href}" x="128" width="128" height="256"/>`);
    }
    const body = frame.parts.body, width = body.width - 20, y = Math.round(frame.rootY) - 36;
    const half = `<svg x="128" y="${y}" width="${width}" height="${body.height}" viewBox="20 0 ${width} ${body.height}" overflow="hidden"><image href="${png(body)}" width="${body.width}" height="${body.height}"/></svg>`;
    images.push(half, `<g transform="translate(256 0) scale(-1 1)">${half}</g>`);
    return images.join("\n");
}

async function exportPng() {
    if (!process.env.IPROP_BROWSER) throw new Error("--png requires IPROP_BROWSER=/path/to/chromium");
    const puppeteer = require(process.env.IPROP_PUPPETEER || "puppeteer-core");
    const browser = await puppeteer.launch({executablePath:process.env.IPROP_BROWSER,headless:true});
    try {
        const page = await browser.newPage();
        await page.setViewport({width:1600,height:1800,deviceScaleFactor:1});
        await page.goto(pathToFileURL(output).href);
        await page.evaluate(() => document.fonts.ready);
        const file = output.replace(/\.svg$/,".png");
        await page.screenshot({path:file,omitBackground:true});
        const preview = {file:path.basename(file),width:1600,height:1800,sha256:digest(fs.readFileSync(file))};
        const social = path.join(root,"website/public/social-preview.png");
        const socialSvg = fs.readFileSync(path.join(root,"website/templates/social-preview.svg"),"utf8")
            .replaceAll("{{LOGO}}","data:image/svg+xml;base64," + fs.readFileSync(path.join(root,"website/public/favicon.svg")).toString("base64"))
            .replaceAll("{{COLLECTION}}","data:image/png;base64," + fs.readFileSync(file).toString("base64"));
        await page.setViewport({width:1200,height:630,deviceScaleFactor:1});
        await page.goto("about:blank");
        await page.setContent(`<style>html,body{margin:0}svg{display:block}</style>${socialSvg}`);
        await page.evaluate(() => document.fonts.ready);
        await page.screenshot({path:social});
        return {preview,social_preview:{file:"website/public/social-preview.png",width:1200,height:630,sha256:digest(fs.readFileSync(social))}};
    } finally {
        await browser.close();
    }
}

async function main() {
    const {frames,worker} = await generate();
    const cards = frames.map((frame,i) => {
        const x = 142 + i % 4 * 332, y = 270 + Math.floor(i / 4) * 338;
        return `<g data-specimen="${i+1}" data-seed="${frame.seed}" transform="translate(${x} ${y})">
          <rect width="316" height="322" rx="9" fill="url(#velvet)" stroke="#637166" stroke-opacity=".28"/>
          <rect x="2" y="2" width="312" height="318" rx="8" fill="url(#weave)" opacity=".34"/>
          <path d="M12 22V12h10M294 12h10v10M304 300v10h-10M22 310H12v-10" fill="none" stroke="#b59b66" stroke-opacity=".48"/>
          <g transform="translate(30 5)" style="image-rendering:pixelated" filter="url(#specimen-shadow)">${specimen(frame)}</g>
          <path d="M158 260v10" stroke="#b3a075" opacity=".42"/>
          <rect x="45" y="274" width="226" height="34" rx="3" fill="url(#label)" stroke="#c4b68d" stroke-opacity=".55"/>
          <text x="59" y="297" font-family="monospace" font-size="18" font-weight="700" fill="#4a493e">${String(i+1).padStart(2,"0")}</text>
          <path d="M90 283v15" stroke="#a69a7b"/>
          <text x="102" y="296" font-family="monospace" font-size="16" letter-spacing=".1" fill="#50564b">SEED ${frame.seed}</text>
        </g>`;
    }).join("\n");
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1800" viewBox="0 0 1600 1800" role="img" aria-labelledby="title description">
      <title id="title">Infinite Props — The Butterfly Collection</title>
      <desc id="description">Sixteen real butterflies from the Infinite Props website, arranged in a four by four walnut and brass collector display. Each specimen is generated from its labelled seed using the website's original WASM worker.</desc>
      <defs>
        <linearGradient id="studio" x2="1" y2="1"><stop stop-color="#1a2324"/><stop offset="1" stop-color="#080e12"/></linearGradient>
        <linearGradient id="wood" x2="1" y2="1"><stop stop-color="#786045"/><stop offset=".14" stop-color="#3c3028"/><stop offset=".53" stop-color="#211d1a"/><stop offset=".88" stop-color="#46382b"/><stop offset="1" stop-color="#877052"/></linearGradient>
        <linearGradient id="brass" x2="1" y2="1"><stop stop-color="#dbcb98"/><stop offset=".24" stop-color="#9c8250"/><stop offset=".6" stop-color="#59482c"/><stop offset="1" stop-color="#c3ac72"/></linearGradient>
        <linearGradient id="velvet" x2=".4" y2="1"><stop stop-color="#233633"/><stop offset="1" stop-color="#13211f"/></linearGradient>
        <linearGradient id="label" x2="0" y2="1"><stop stop-color="#e9e1c8"/><stop offset="1" stop-color="#c9c2a7"/></linearGradient>
        <linearGradient id="glass" x2="1" y2="1"><stop stop-color="#fff" stop-opacity=".055"/><stop offset=".3" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#adc9c1" stop-opacity=".015"/></linearGradient>
        <pattern id="grain" width="9" height="37" patternUnits="userSpaceOnUse"><path d="M1 0v37M5 0v37" stroke="#bd9b65" stroke-opacity=".06"/></pattern>
        <pattern id="weave" width="5" height="5" patternUnits="userSpaceOnUse"><path d="M0 1h5M1 0v5" stroke="#98b6a7" stroke-width=".5" stroke-opacity=".16"/></pattern>
        <filter id="case-shadow" x="-.2" y="-.1" width="1.4" height="1.3"><feDropShadow dx="0" dy="22" stdDeviation="24" flood-color="#000" flood-opacity=".65"/></filter>
        <filter id="specimen-shadow" x="-.2" y="-.2" width="1.4" height="1.5"><feDropShadow dx="0" dy="6" stdDeviation="4" flood-color="#000" flood-opacity=".65"/></filter>
      </defs>
      <rect width="1600" height="1800" rx="24" fill="url(#studio)"/>
      <g filter="url(#case-shadow)"><rect x="62" y="54" width="1476" height="1684" rx="22" fill="url(#wood)" stroke="#9d835c" stroke-width="2"/></g>
      <rect x="72" y="64" width="1456" height="1664" rx="17" fill="url(#grain)"/>
      <rect x="92" y="84" width="1416" height="1624" rx="12" fill="url(#brass)"/>
      <rect x="96" y="88" width="1408" height="1616" rx="10" fill="#111d1c" stroke="#080e0d" stroke-width="5"/>
      <rect x="110" y="102" width="1380" height="1588" rx="7" fill="url(#velvet)"/>
      <rect x="110" y="102" width="1380" height="1588" rx="7" fill="url(#weave)"/>
      <path d="M142 226h1312" stroke="#bca774" stroke-opacity=".35"/>
      <text x="145" y="149" font-family="Arial,Helvetica,sans-serif" font-size="17" letter-spacing="4" fill="#b9a779">INFINITE PROPS / GENERATIVE COLLECTION No. 01</text>
      <text x="142" y="198" font-family="Georgia,serif" font-size="45" fill="#f0ead8">The Butterfly Collection</text>
      <text x="1454" y="161" text-anchor="end" font-family="monospace" font-size="17" letter-spacing="2" fill="#d5c89d">16 SPECIMENS</text>
      <text x="1454" y="193" text-anchor="end" font-family="monospace" font-size="16" fill="#a8b7a9">4 × 4 / 16 SEEDS</text>
      ${cards}
      <rect x="110" y="102" width="1380" height="1588" rx="7" fill="url(#glass)" pointer-events="none"/>
      <path d="M122 114h1356" stroke="#fff3c9" stroke-opacity=".2"/>
      <text x="800" y="1662" text-anchor="middle" font-family="Arial,Helvetica,sans-serif" font-size="16" letter-spacing="3" fill="#a9b6a7">GENERATED BY THE LIVE WEBSITE DEMO · INFINITEPROPS.DEV</text>
      <g fill="#b49b65" stroke="#28251b" stroke-width="2">${[[80,72],[1520,72],[80,1720],[1520,1720]].map(([x,y])=>`<circle cx="${x}" cy="${y}" r="5"/><path d="M${x-2} ${y+2}l4-4" stroke="#4b412d"/>`).join("")}</g>
      <metadata>Copyright 2026 jakubczachor. All rights reserved. See examples/showcase/LICENSE.md.</metadata>
    </svg>\n`;
    fs.writeFileSync(output,svg);
    const images = args.includes("--png") ? await exportPng() : {};
    fs.writeFileSync(output.replace(/\.svg$/,".json"),JSON.stringify({
        description:"Sixteen original website butterflies in a 4x4 collector display",
        generator:"scripts/render_readme_collection.cjs", worker_sha256:digest(worker),
        assembly_source:"website/src/butterfly.ts:makeButterfly", rows:4, columns:4,
        programs:JSON.parse(fs.readFileSync(path.join(root,"examples/showcase/butterfly-benchmark.json"))).parts,
        specimens:frames.map(frame=>({seed:frame.seed,style:frame.style,scheme:frame.scheme,rootY:frame.rootY,
            parts:Object.fromEntries(Object.entries(frame.parts).map(([name,part])=>[name,{width:part.width,height:part.height,rgba_sha256:digest(part.data)}]))})),
        svg_sha256:digest(svg), ...images, license:"All rights reserved; examples/showcase/LICENSE.md",
    },null,2)+"\n");
    console.log(`Rendered ${frames.length} original butterflies: ${output} (${Buffer.byteLength(svg)} bytes)`);
}
main().catch(error=>{console.error(error);process.exitCode=1;});

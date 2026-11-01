// ---------------------------------------------------------------------------
// static/shorts/renderers/memory_map_view.js
//
// TWO MEMORIES, SIDE BY SIDE.
//
// Flash on the left, RAM on the right, sections drawn where they really
// sit. One picture carries most of this journey, because almost every
// question it answers is "where does this live":
//
//   the vector table    the first sixteen bytes of flash, and what the
//                       chip reads out of them at reset
//   .text               code, in flash, executed in place
//   .data               in BOTH columns -- stored on the left, running
//                       on the right, with the startup copy animating
//                       between them
//   .bss                right column only, because zero needs no storage
//
// THREE VIEWS
//   map        the two memories, with the copy and zero animations
//   encodings  the same source line as x86-64 and as Thumb-2
//   steps      one build command, chip against PC
//
// WHAT IT DOES NOT DO
//   It does not scale sections to real proportion. 192 bytes inside 512K
//   of flash is 0.04% -- drawn to scale the entire program is a line
//   thinner than the stroke drawing it. So sections are drawn at legible
//   heights and the REAL numbers are printed beside them, with the true
//   occupancy shown separately as a percentage. A map that lied about
//   proportion without saying so would be the dishonest version; saying
//   so costs one line of text.
// ---------------------------------------------------------------------------

(function () {
"use strict";

const SECTION_COLOUR = {
    ".isr_vector": "#e0a33a",
    ".text":       null,          // takes the live theme accent
    ".data":       "#4fd1ff",
    ".bss":        "#9b8cf0",
};

registerRenderingAlgorithm("memory_map_view", {
    render(container, dspOutput, config, sampleId, fs, durationMs) {
        container.innerHTML = "";
        container.style.position = "relative";

        const VIEWS = { map: 1, encodings: 1, steps: 1 };
        const view = (config && config.view) || "map";
        const fo = dspOutput?.finalOutput ?? {};

        if (!VIEWS[view]) {
            container.innerHTML =
                `<p style="color:#ff4d4d;padding:20px;font-family:monospace;">
                 memory_map_view: unknown view "${view}". Known:
                 ${Object.keys(VIEWS).join(", ")}.</p>`;
            return;
        }
        if (fo.error || !fo.build) {
            container.innerHTML =
                `<p style="color:#ff4d4d;padding:20px;font-family:monospace;">
                 memory_map_view: ${fo.error || "no build data"}.</p>`;
            return;
        }

        const useDPR = globalThis._DEV_CANVAS_DPR ?? devicePixelRatio;
        const canvas = document.createElement("canvas");
        canvas.style.cssText = "width:100%;height:100%;display:block;";
        container.appendChild(canvas);
        const sizeTo = (w, h) => {
            canvas.width  = Math.round(w * useDPR);
            canvas.height = Math.round(h * useDPR);
            canvas.style.width = w + "px";
            canvas.style.height = h + "px";
        };
        sizeTo(container.offsetWidth || 390, container.offsetHeight || 620);

        const root   = getComputedStyle(document.documentElement);
        const accent = (typeof THEMES !== "undefined"
                        && THEMES[themeIndex]?.traceColor)
                       || root.getPropertyValue("--accent").trim() || "#2edac4";
        const dim    = root.getPropertyValue("--text-dim").trim() || "#9aa4b0";
        const text   = root.getPropertyValue("--text").trim() || "#d8dee9";
        const warn   = root.getPropertyValue("--warn").trim() || "#c98a1f";
        const edge   = "#2b3442";

        const mono = (s, w) => `${w ? w + " " : ""}${s * useDPR}px `
                             + `"IBM Plex Mono", ui-monospace, monospace`;
        const sans = (s, w) => `${w ? w + " " : ""}${s * useDPR}px `
                             + `"IBM Plex Sans", sans-serif`;
        const colourOf = (n) => SECTION_COLOUR[n] ?? accent;

        let lastProgress = 0;

        function frame() {
            const W = canvas.width, H = canvas.height;
            const M = { l: 14 * useDPR, r: 14 * useDPR,
                        t: 58 * useDPR, b: 40 * useDPR };
            return { W, H, M, pw: W - M.l - M.r, ph: H - M.t - M.b };
        }

        function fit(ctx, s, maxW) {
            if (ctx.measureText(s).width <= maxW) return s;
            let t = s;
            while (t.length > 3 && ctx.measureText(t + "…").width > maxW) {
                t = t.slice(0, -1);
            }
            return t + "…";
        }

        function wrap(ctx, s, maxW, maxLines) {
            const out = []; let line = "";
            for (const w of String(s).split(/\s+/)) {
                const t = line ? line + " " + w : w;
                if (ctx.measureText(t).width > maxW && line) {
                    out.push(line); line = w;
                    if (out.length === maxLines) return out;
                } else line = t;
            }
            if (line && out.length < maxLines) out.push(line);
            return out;
        }

        function header(ctx, g, title, sub) {
            ctx.fillStyle = accent;
            ctx.font = sans(13, "600");
            ctx.fillText(fit(ctx, title, g.pw), g.M.l, 22 * useDPR);
            ctx.fillStyle = dim;
            ctx.font = sans(10.5);
            let y = 37 * useDPR;
            for (const ln of wrap(ctx, sub || "", g.pw, 2)) {
                ctx.fillText(ln, g.M.l, y); y += 12 * useDPR;
            }
        }

        function footer(ctx, g) {
            ctx.save();
            ctx.font = mono(8, "600");
            ctx.fillStyle = dim;
            ctx.fillText(fo.toolchain || "", g.M.l, g.H - 12 * useDPR);
            ctx.textAlign = "right";
            ctx.fillStyle = warn;
            ctx.globalAlpha = 0.9;
            ctx.fillText("MEASURED FROM A REAL ELF", g.W - g.M.r, g.H - 12 * useDPR);
            ctx.restore();
        }

        // ── The map ───────────────────────────────────────────────────
        function drawMap(ctx, g, prog) {
            const colW = (g.pw - 26 * useDPR) / 2;
            const xF = g.M.l, xR = g.M.l + colW + 26 * useDPR;
            // Centre the group vertically. Top-aligned, a compact map
            // left most of a 620px card empty and read as a loading
            // state rather than a finished one.
            const top0 = g.M.t + 26 * useDPR;
            const rows = Math.max(fo.flashRows.length, fo.ramRows.length, 1);
            // Tall enough for the rows plus a little air, never taller
            // than the card. Drawn at full height the columns were four
            // fifths empty and the card looked unfinished rather than
            // sparse.
            const rowHc = Math.min(62 * useDPR, (g.ph - 70 * useDPR) / rows);
            const colH = Math.min(g.ph - 58 * useDPR, rows * rowHc + 10 * useDPR);
            const slack = (g.ph - 58 * useDPR) - colH;
            const top = top0 + Math.max(0, slack * 0.40);

            const label = (x, t, sub) => {
                ctx.textAlign = "left";
                ctx.fillStyle = text;
                ctx.font = mono(9.5, "600");
                ctx.fillText(t, x, g.M.t + 10 * useDPR);
                ctx.fillStyle = dim;
                ctx.font = mono(7.5);
                ctx.fillText(sub, x, g.M.t + 20 * useDPR);
            };
            label(xF, "FLASH", `${(fo.memory.flashSize / 1024) | 0}K @ 0x08000000`);
            label(xR, "RAM",   `${(fo.memory.ramSize / 1024) | 0}K @ 0x20000000`);

            // The outlines. Drawn first so sections sit inside them.
            ctx.strokeStyle = edge;
            ctx.lineWidth = 1 * useDPR;
            ctx.strokeRect(xF, top, colW, colH);
            ctx.strokeRect(xR, top, colW, colH);

            // Sections are stacked from the top at LEGIBLE heights, not
            // to scale -- see the header comment. The real size is
            // printed on each one.
            const rowH = rowHc;
            const drawCol = (x, rows, fade) => {
                let y = top + 4 * useDPR;
                for (const r of rows) {
                    const c = colourOf(r.name);
                    ctx.save();
                    ctx.globalAlpha = fade ? fade(r) : 1;
                    ctx.fillStyle = c;
                    ctx.globalAlpha *= 0.22;
                    ctx.fillRect(x + 3 * useDPR, y, colW - 6 * useDPR, rowH - 5 * useDPR);
                    ctx.globalAlpha = fade ? fade(r) : 1;
                    ctx.strokeStyle = c;
                    ctx.lineWidth = 1.2 * useDPR;
                    ctx.strokeRect(x + 3 * useDPR, y, colW - 6 * useDPR, rowH - 5 * useDPR);

                    ctx.fillStyle = c;
                    ctx.font = mono(9, "600");
                    ctx.fillText(fit(ctx, r.name, colW - 14 * useDPR),
                                 x + 8 * useDPR, y + 14 * useDPR);
                    ctx.fillStyle = dim;
                    ctx.font = mono(7.5);
                    const addr = "0x" + (r.addr >>> 0).toString(16).toUpperCase()
                                        .padStart(8, "0");
                    ctx.fillText(fit(ctx, `${addr}  ${r.size}B`, colW - 14 * useDPR),
                                 x + 8 * useDPR, y + 25 * useDPR);
                    ctx.restore();
                    r._y = y; r._h = rowH - 5 * useDPR; r._x = x;
                    y += rowH;
                }
            };

            // .data in RAM, and .bss, only exist AFTER startup has run.
            // Fading them in across the sweep is the startup code doing
            // its job, not decoration.
            const copyAt = 0.45, zeroAt = 0.75;
            const emptyNote = (x, msg) => {
                ctx.save();
                ctx.fillStyle = dim;
                ctx.globalAlpha = 0.55;
                ctx.font = mono(8);
                ctx.textAlign = "center";
                ctx.fillText(msg, x + colW / 2, top + colH / 2);
                ctx.textAlign = "left";
                ctx.restore();
            };
            if (!fo.ramRows.length) {
                emptyNote(xR, "nothing here until startup runs");
            }
            drawCol(xF, fo.flashRows, null);
            drawCol(xR, fo.ramRows, (r) => {
                if (r.name === ".data") return prog < copyAt ? 0.12 : 1;
                if (r.name === ".bss")  return prog < zeroAt ? 0.12 : 1;
                return 1;
            });

            // The copy arrow. This is the one thing on the card that is
            // an EVENT rather than a layout.
            const srcRow = fo.flashRows.find(r => r.name === ".data" && r.stored);
            const dstRow = fo.ramRows.find(r => r.name === ".data");
            if (fo.copy && srcRow && dstRow && prog > copyAt - 0.25) {
                const t = Math.min(1, Math.max(0, (prog - (copyAt - 0.25)) / 0.25));
                const y0 = srcRow._y + srcRow._h / 2;
                const y1 = dstRow._y + dstRow._h / 2;
                const x0 = xF + colW - 3 * useDPR, x1 = xR + 3 * useDPR;
                ctx.save();
                ctx.strokeStyle = SECTION_COLOUR[".data"];
                ctx.globalAlpha = 0.9;
                ctx.lineWidth = 1.6 * useDPR;
                ctx.setLineDash([3 * useDPR, 3 * useDPR]);
                ctx.beginPath();
                ctx.moveTo(x0, y0);
                ctx.lineTo(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
                ctx.stroke();
                ctx.setLineDash([]);
                if (t >= 1) {
                    ctx.beginPath();
                    ctx.moveTo(x1, y1);
                    ctx.lineTo(x1 - 6 * useDPR, y1 - 4 * useDPR);
                    ctx.lineTo(x1 - 6 * useDPR, y1 + 4 * useDPR);
                    ctx.closePath();
                    ctx.fillStyle = SECTION_COLOUR[".data"];
                    ctx.fill();
                }
                ctx.font = mono(7.5, "600");
                ctx.fillStyle = SECTION_COLOUR[".data"];
                ctx.textAlign = "center";
                ctx.fillText(`copy ${fo.copy.size}B`, (x0 + x1) / 2, y0 - 6 * useDPR);
                ctx.textAlign = "left";
                ctx.restore();
            }

            // The sums, spelled out rather than totalled.
            const yb = top + colH + 14 * useDPR;
            ctx.font = mono(8, "600");
            ctx.fillStyle = dim;
            const c = fo.cost;
            ctx.fillText(fit(ctx, `text ${c.flash.text} + data ${c.flash.data}`
                             + ` = ${c.flash.total}B`, colW), xF, yb);
            ctx.fillText(fit(ctx, `data ${c.ram.data} + bss ${c.ram.bss}`
                             + ` = ${c.ram.total}B`, colW), xR, yb);
            ctx.save();
            ctx.fillStyle = dim;
            ctx.globalAlpha = 0.75;
            ctx.font = mono(7.5);
            ctx.fillText(`${(c.flashPct * 100).toFixed(2)}% of 512K flash`,
                         xF, yb + 11 * useDPR);
            ctx.fillText(`${(c.ramPct * 100).toFixed(2)}% of 96K RAM`,
                         xR, yb + 11 * useDPR);
            ctx.globalAlpha = 0.55;
            ctx.fillText("boxes are not to scale — real sizes printed on each",
                         xF, yb + 22 * useDPR);
            ctx.restore();
        }

        // ── The vector table, drawn as two words ──────────────────────
        function drawVector(ctx, g, prog) {
            const v = fo.vector;
            const w = Math.min(g.pw, 300 * useDPR);
            const x = g.M.l, y = g.M.t + 40 * useDPR;
            const rowH = 42 * useDPR;
            const words = [
                { hex: v.spHex, what: "initial stack pointer",
                  note: `top of RAM (${v.stackTopHex})`, c: SECTION_COLOUR[".bss"] },
                { hex: v.entryHex, what: "reset handler",
                  note: `${v.handlerHex} with bit 0 set`, c: warn },
            ];
            ctx.font = mono(8, "600");
            ctx.fillStyle = dim;
            ctx.fillText("THE CHIP READS TWO WORDS FROM 0x08000000", x, y - 14 * useDPR);

            words.forEach((wd, i) => {
                const yy = y + i * (rowH + 10 * useDPR);
                const on = prog > (i === 0 ? 0.15 : 0.45);
                ctx.save();
                ctx.globalAlpha = on ? 1 : 0.2;
                ctx.strokeStyle = wd.c; ctx.lineWidth = 1.3 * useDPR;
                ctx.fillStyle = wd.c;
                ctx.globalAlpha *= 0.14;
                ctx.fillRect(x, yy, w, rowH);
                ctx.globalAlpha = on ? 1 : 0.2;
                ctx.strokeRect(x, yy, w, rowH);
                ctx.fillStyle = wd.c;
                ctx.font = mono(15, "600");
                ctx.fillText(wd.hex, x + 10 * useDPR, yy + 20 * useDPR);
                ctx.fillStyle = text;
                ctx.font = sans(9.5);
                ctx.fillText(wd.what, x + 10 * useDPR, yy + 33 * useDPR);
                ctx.fillStyle = dim;
                ctx.font = mono(7.5);
                ctx.textAlign = "right";
                ctx.fillText(wd.note, x + w - 10 * useDPR, yy + 33 * useDPR);
                ctx.textAlign = "left";
                ctx.restore();
            });

            if (prog > 0.72) {
                const yy = y + 2 * (rowH + 10 * useDPR) + 6 * useDPR;
                ctx.fillStyle = warn;
                ctx.font = mono(9, "600");
                ctx.fillText("bit 0 = 1", x, yy + 12 * useDPR);
                ctx.fillStyle = text;
                ctx.font = sans(10);
                for (const [i, ln] of wrap(ctx,
                        "Not an off-by-one. On ARMv7-M bit 0 of a branch "
                      + "target selects the instruction set, and Cortex-M "
                      + "runs Thumb only. Clear it and the chip faults on "
                      + "the first instruction it fetches.",
                        g.pw, 4).entries()) {
                    ctx.fillText(ln, x, yy + (26 + i * 13) * useDPR);
                }
            }
        }

        // ── Two encodings of one line ─────────────────────────────────
        function drawEncodings(ctx, g, prog) {
            const e = fo.encodings;
            if (!e) return;
            let y = g.M.t + 6 * useDPR;

            ctx.fillStyle = accent;
            ctx.font = mono(11, "600");
            ctx.fillText(fit(ctx, e.line, g.pw), g.M.l, y);
            y += 13 * useDPR;
            ctx.fillStyle = dim;
            ctx.font = sans(9.5);
            ctx.fillText(fit(ctx, e.note, g.pw), g.M.l, y);
            y += 20 * useDPR;

            const block = (title, side, from, c) => {
                const on = prog > from;
                ctx.save();
                ctx.globalAlpha = on ? 1 : 0.15;
                ctx.fillStyle = c;
                ctx.font = mono(9, "600");
                ctx.fillText(title, g.M.l, y);
                y += 12 * useDPR;
                // Column widths measured from the WIDEST entry rather
                // than guessed: the first version clipped every ARM
                // mnemonic to "ldr" and dropped the operands, which is
                // the only part of an encoding card that matters.
                ctx.font = mono(7.5);
                let hexW = 0;
                for (const i of side.instructions) {
                    hexW = Math.max(hexW, ctx.measureText(i.hex).width);
                }
                const xHex = g.M.l + 20 * useDPR;
                const xTxt = xHex + hexW + 10 * useDPR;
                for (const i of side.instructions) {
                    ctx.fillStyle = dim;
                    ctx.font = mono(7);
                    ctx.fillText(`${i.bytes}B`, g.M.l, y);
                    ctx.fillStyle = c;
                    ctx.globalAlpha = (on ? 1 : 0.15) * 0.85;
                    ctx.font = mono(7.5);
                    ctx.fillText(i.hex, xHex, y);
                    ctx.globalAlpha = on ? 1 : 0.15;
                    ctx.fillStyle = text;
                    // Drop objdump's "@ (addr <sym>)" aside -- it is a
                    // disassembler convenience, not part of the encoding.
                    const t = i.text.replace(/\s*@\s.*$/, "");
                    ctx.fillText(fit(ctx, t, g.W - g.M.r - xTxt), xTxt, y);
                    y += 11.5 * useDPR;
                }
                ctx.fillStyle = dim;
                ctx.font = mono(7.5);
                ctx.fillText(`widths seen: ${side.widths.join(", ")} bytes`,
                             g.M.l, y + 2 * useDPR);
                y += 22 * useDPR;
                ctx.restore();
            };

            block(e.pc.label, e.pc, 0.1, "#4fd1ff");
            block(e.arm.label, e.arm, 0.45, accent);

            if (prog > 0.72) {
                ctx.fillStyle = text;
                ctx.font = sans(10);
                for (const [i, ln] of wrap(ctx,
                        "x86 puts the whole 32-bit address inside the "
                      + "instruction, so that one costs seven bytes. ARM "
                      + "cannot — a 32-bit constant will not fit in a "
                      + "32-bit instruction — so it loads it from a pool "
                      + "placed after the code. Same source line.",
                        g.pw, 5).entries()) {
                    ctx.fillText(ln, g.M.l, y + i * 13 * useDPR);
                }
            }
        }

        // ── One build step, chip against PC ───────────────────────────
        function drawStep(ctx, g, prog) {
            const s = fo.step;
            if (!s) return;
            let y = g.M.t + 10 * useDPR;
            const row = (tag, cmd, c, from) => {
                const on = prog > from;
                ctx.save();
                ctx.globalAlpha = on ? 1 : 0.18;
                ctx.fillStyle = c;
                ctx.font = mono(8.5, "600");
                ctx.fillText(tag, g.M.l, y);
                y += 13 * useDPR;
                ctx.fillStyle = c;
                ctx.globalAlpha = (on ? 1 : 0.18) * 0.16;
                const lines = wrap(ctx, cmd, g.pw - 16 * useDPR, 5);
                ctx.fillRect(g.M.l, y - 10 * useDPR, g.pw,
                             lines.length * 13 * useDPR + 10 * useDPR);
                ctx.globalAlpha = on ? 1 : 0.18;
                ctx.font = mono(8.5);
                ctx.fillStyle = text;
                for (const ln of lines) {
                    ctx.fillText(ln, g.M.l + 8 * useDPR, y); y += 13 * useDPR;
                }
                y += 16 * useDPR;
                ctx.restore();
            };
            row("ON THE CHIP", s.chip, accent, 0.1);
            row("ON A PC", s.pc, "#4fd1ff", 0.45);

            if (prog > 0.65) {
                ctx.fillStyle = text;
                ctx.font = sans(10.5);
                for (const [i, ln] of wrap(ctx, s.why, g.pw, 6).entries()) {
                    ctx.fillText(ln, g.M.l, y + i * 13.5 * useDPR);
                }
            }
        }

        function draw(progress) {
            lastProgress = progress;
            const ctx = canvas.getContext("2d");
            const g = frame();
            ctx.clearRect(0, 0, g.W, g.H);

            const sc = fo.scenario || {};
            if (view === "map") {
                const isVector = (fo.flashRows || []).length === 1;
                if (isVector) {
                    header(ctx, g, "Where it starts",
                           "Power-on, and the chip has sixteen bytes to go on.");
                    drawVector(ctx, g, progress);
                } else {
                    header(ctx, g, sc.name || "The memory map", sc.blurb || "");
                    drawMap(ctx, g, progress);
                }
            } else if (view === "encodings") {
                header(ctx, g, "One line, two machines",
                       "The same C, compiled for each.");
                drawEncodings(ctx, g, progress);
            } else {
                header(ctx, g, fo.step ? fo.step.title : "Build step",
                       "What the command is for.");
                drawStep(ctx, g, progress);
            }
            footer(ctx, g);
        }

        // Resizing a canvas clears it, so whatever resizes it owns
        // repainting it.
        new ResizeObserver(e => {
            const { width, height } = e[0].contentRect;
            if (!width || !height) return;
            sizeTo(width, height);
            draw(lastProgress);
        }).observe(container);

        draw(0);
        startSweepLoop(sampleId, durationMs, (elapsed) => {
            draw(Math.max(0, Math.min(1, elapsed / durationMs)));
        });
    },
});

})();

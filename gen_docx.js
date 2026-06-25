const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType,
  HeadingLevel, BorderStyle, WidthType, ShadingType,
  PageNumber,
} = require("docx");

// ── 配色 ──
const FONT_BODY = "Microsoft YaHei";
const FONT_HEADING = "Microsoft YaHei";
const FONT_CODE = "Consolas";
const COLOR_H1 = "1a5276";
const COLOR_H2 = "2471a3";
const COLOR_H3 = "2e86c1";
const COLOR_CODE_BG = "f4f6f7";
const COLOR_TABLE_HEADER = "2e86c1";
const COLOR_TABLE_ALT = "eaf2f8";
const COLOR_LINK = "2471a3";

const thinBorder = { style: BorderStyle.SINGLE, size: 1, color: "b0b0b0" };
const cellBorders = { top: thinBorder, bottom: thinBorder, left: thinBorder, right: thinBorder };
const noBorder = { style: BorderStyle.NONE, size: 0 };
const noBorders = { top: noBorder, bottom: noBorder, left: noBorder, right: noBorder };

// ── 内联文本解析 → TextRun[] ──
// 用 token 流法，避免占位符与纯数字文本冲突
function inlineRuns(text, opts = {}) {
  const size = opts.size || 21;
  const color = opts.color;
  const bold = opts.bold;
  const font = opts.font || FONT_BODY;
  const runs = [];
  // 正则同时匹配 code/bold，按出现位置依次处理
  const re = /`([^`]+)`|\*\*([^*]+?)\*\*/g;
  let last = 0;
  let m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) {
      runs.push(new TextRun({ text: text.slice(last, m.index), font, size, bold, color }));
    }
    if (m[1] !== undefined) {
      // 行内代码
      runs.push(new TextRun({ text: m[1], font: FONT_CODE, size: size - 2, color: "c0392b" }));
    } else if (m[2] !== undefined) {
      // 粗体
      runs.push(new TextRun({ text: m[2], font, size, bold: true, color }));
    }
    last = re.lastIndex;
  }
  if (last < text.length) {
    runs.push(new TextRun({ text: text.slice(last), font, size, bold, color }));
  }
  return runs;
}

// ── 判断一行是不是表格分隔行 ──
function isTableSeparator(line) {
  return /^\|[\s\-:|]+\|$/.test(line.trim());
}

// ── 解析表格单元行 ──
function splitTableRow(line) {
  return line.split("|").slice(1, -1).map(c => c.trim());
}

// ── 读取并按行分割 ──
const md = fs.readFileSync("项目技术文档.md", "utf-8");
const lines = md.split(/\r?\n/);
const N = lines.length;

// ── 主解析循环（显式 i++，绝不死循环）──
const blocks = [];
let i = 0;
let safety = 0;

while (i < N) {
  if (++safety > 100000) { console.error("解析保护触发, i=" + i); break; }
  const line = lines[i];

  // 空行
  if (line.trim() === "") { i++; continue; }

  // 标题（#### 视为 h3，#### 以下忽略前缀）
  if (/^####+ /.test(line)) {
    const level = line.match(/^#+/)[0].length;
    const text = line.replace(/^#+\s/, "");
    if (level >= 4) blocks.push({ t: "h3", text });
    else if (level === 3) blocks.push({ t: "h3", text });
    else if (level === 2) blocks.push({ t: "h2", text });
    else blocks.push({ t: "h1", text });
    i++; continue;
  }
  if (/^### /.test(line)) { blocks.push({ t: "h3", text: line.slice(4) }); i++; continue; }
  if (/^## /.test(line))  { blocks.push({ t: "h2", text: line.slice(3) }); i++; continue; }
  if (/^# /.test(line))   { blocks.push({ t: "h1", text: line.slice(2) }); i++; continue; }

  // 水平线
  if (/^---+$/.test(line.trim())) { blocks.push({ t: "hr" }); i++; continue; }

  // 引用块
  if (/^> /.test(line)) {
    const buf = [];
    while (i < N && /^> /.test(lines[i])) { buf.push(lines[i].slice(2)); i++; }
    blocks.push({ t: "quote", lines: buf });
    continue;
  }

  // 代码块
  if (/^```/.test(line)) {
    i++;
    const buf = [];
    while (i < N && !/^```/.test(lines[i])) { buf.push(lines[i]); i++; }
    if (i < N) i++; // 跳过闭合 ```
    blocks.push({ t: "code", text: buf.join("\n") });
    continue;
  }

  // 表格（当前行以 | 开头，且下一行是分隔行）
  if (/^\|/.test(line) && i + 1 < N && isTableSeparator(lines[i + 1])) {
    const header = splitTableRow(lines[i]);
    i += 2;
    const rows = [];
    while (i < N && /^\|/.test(lines[i])) { rows.push(splitTableRow(lines[i])); i++; }
    blocks.push({ t: "table", header, rows });
    continue;
  }

  // 列表项
  if (/^[-*]\s/.test(line.trim()) || /^\d+\.\s/.test(line.trim())) {
    const items = [];
    while (i < N) {
      const trimmed = lines[i].trim();
      if (/^[-*]\s/.test(trimmed)) {
        items.push({ ordered: false, text: trimmed.replace(/^[-*]\s/, "") });
        i++;
      } else if (/^\d+\.\s/.test(trimmed)) {
        items.push({ ordered: true, text: trimmed.replace(/^\d+\.\s/, "") });
        i++;
      } else {
        break;
      }
    }
    blocks.push({ t: "list", items });
    continue;
  }

  // 普通段落：累积连续的非特殊行
  {
    const buf = [];
    while (i < N) {
      const l = lines[i];
      const lt = l.trim();
      if (lt === "") break;
      if (/^#/.test(lt)) break;                    // 任何级别标题
      if (/^---+$/.test(lt)) break;                // 水平线
      if (/^> /.test(l)) break;
      if (/^```/.test(l)) break;
      if (/^\|/.test(l)) break;
      if (/^[-*]\s/.test(lt) || /^\d+\.\s/.test(lt)) break;
      buf.push(l);
      i++;
    }
    if (buf.length > 0) {
      blocks.push({ t: "para", text: buf.join("\n") });
    } else {
      // 防御：遇到无法识别的行，强制跳过避免死循环
      i++;
    }
  }
}

console.log(`解析完成: ${N} 行 → ${blocks.length} 个块`);

// ── 构建文档子元素 ──
const children = [];
const CONTENT_WIDTH = 9026; // A4 - 1寸边距

for (const b of blocks) {
  switch (b.t) {
    case "h1":
      children.push(new Paragraph({
        heading: HeadingLevel.HEADING_1,
        spacing: { before: 480, after: 200 },
        border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: COLOR_H1, space: 6 } },
        children: inlineRuns(b.text, { size: 36, bold: true, color: COLOR_H1, font: FONT_HEADING }),
      }));
      break;

    case "h2":
      children.push(new Paragraph({
        heading: HeadingLevel.HEADING_2,
        spacing: { before: 360, after: 160 },
        children: inlineRuns(b.text, { size: 28, bold: true, color: COLOR_H2, font: FONT_HEADING }),
      }));
      break;

    case "h3":
      children.push(new Paragraph({
        heading: HeadingLevel.HEADING_3,
        spacing: { before: 280, after: 120 },
        children: inlineRuns(b.text, { size: 24, bold: true, color: COLOR_H3, font: FONT_HEADING }),
      }));
      break;

    case "para": {
      const plines = b.text.split("\n");
      plines.forEach((l, idx) => {
        children.push(new Paragraph({
          spacing: { before: idx === 0 ? 60 : 0, after: 60, line: 360 },
          children: inlineRuns(l),
        }));
      });
      break;
    }

    case "quote": {
      // 引用：浅蓝底色单列表格
      const quoteParas = b.lines.map(l => new Paragraph({
        spacing: { line: 320, after: 40 },
        children: inlineRuns(l, { size: 20, color: "2c3e50" }),
      }));
      children.push(new Table({
        width: { size: CONTENT_WIDTH, type: WidthType.DXA },
        columnWidths: [CONTENT_WIDTH],
        rows: [new TableRow({
          children: [new TableCell({
            borders: {
              top: noBorder, bottom: noBorder, right: noBorder,
              left: { style: BorderStyle.SINGLE, size: 18, color: COLOR_TABLE_HEADER },
            },
            width: { size: CONTENT_WIDTH, type: WidthType.DXA },
            shading: { fill: "eaf2f8", type: ShadingType.CLEAR },
            margins: { top: 100, bottom: 100, left: 200, right: 200 },
            children: quoteParas,
          })],
        })],
      }));
      children.push(new Paragraph({ spacing: { after: 100 }, children: [] }));
      break;
    }

    case "code": {
      const codeParas = b.text.split("\n").map(cl => new Paragraph({
        spacing: { line: 260 },
        children: [new TextRun({ text: cl || " ", font: FONT_CODE, size: 17, color: "1a202c" })],
      }));
      children.push(new Table({
        width: { size: CONTENT_WIDTH, type: WidthType.DXA },
        columnWidths: [CONTENT_WIDTH],
        rows: [new TableRow({
          children: [new TableCell({
            borders: cellBorders,
            width: { size: CONTENT_WIDTH, type: WidthType.DXA },
            shading: { fill: COLOR_CODE_BG, type: ShadingType.CLEAR },
            margins: { top: 120, bottom: 120, left: 180, right: 180 },
            children: codeParas,
          })],
        })],
      }));
      children.push(new Paragraph({ spacing: { after: 100 }, children: [] }));
      break;
    }

    case "table": {
      const colCount = b.header.length;
      const colW = Math.floor(CONTENT_WIDTH / colCount);
      const colWidths = Array(colCount).fill(colW);

      const rows = [];
      // 表头
      rows.push(new TableRow({
        tableHeader: true,
        children: b.header.map((cell, ci) => new TableCell({
          borders: cellBorders,
          width: { size: colWidths[ci], type: WidthType.DXA },
          shading: { fill: COLOR_TABLE_HEADER, type: ShadingType.CLEAR },
          verticalAlign: "center",
          margins: { top: 60, bottom: 60, left: 100, right: 100 },
          children: [new Paragraph({
            alignment: ci === 0 ? AlignmentType.LEFT : AlignmentType.CENTER,
            spacing: { line: 280 },
            children: inlineRuns(cell, { size: 19, bold: true, color: "ffffff" }),
          })],
        })),
      }));
      // 数据行
      b.rows.forEach((row, ri) => {
        const bg = ri % 2 === 0 ? COLOR_TABLE_ALT : "ffffff";
        // 补齐列数
        const cells = row.slice();
        while (cells.length < colCount) cells.push("");
        rows.push(new TableRow({
          children: cells.map((cell, ci) => new TableCell({
            borders: cellBorders,
            width: { size: colWidths[ci], type: WidthType.DXA },
            shading: { fill: bg, type: ShadingType.CLEAR },
            verticalAlign: "center",
            margins: { top: 60, bottom: 60, left: 100, right: 100 },
            children: [new Paragraph({
              alignment: ci === 0 ? AlignmentType.LEFT : AlignmentType.CENTER,
              spacing: { line: 280 },
              children: inlineRuns(cell, { size: 19 }),
            })],
          })),
        }));
      });

      children.push(new Paragraph({ spacing: { before: 80 }, children: [] }));
      children.push(new Table({
        width: { size: CONTENT_WIDTH, type: WidthType.DXA },
        columnWidths: colWidths,
        rows,
      }));
      children.push(new Paragraph({ spacing: { after: 120 }, children: [] }));
      break;
    }

    case "list": {
      b.items.forEach(item => {
        children.push(new Paragraph({
          spacing: { before: 40, after: 40, line: 340 },
          indent: { left: 420, hanging: 210 },
          children: [
            new TextRun({ text: "• ", font: FONT_BODY, size: 21, color: COLOR_LINK }),
            ...inlineRuns(item.text),
          ],
        }));
      });
      break;
    }

    case "hr":
      children.push(new Paragraph({
        spacing: { before: 200, after: 200 },
        border: { bottom: { style: BorderStyle.SINGLE, size: 2, color: "cccccc", space: 1 } },
        children: [],
      }));
      break;
  }
}

console.log(`构建 ${children.length} 个元素`);

// ── 组装文档 ──
const doc = new Document({
  styles: {
    default: {
      document: { run: { font: FONT_BODY, size: 21 }, paragraph: { spacing: { line: 360 } } },
    },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 36, bold: true, font: FONT_HEADING, color: COLOR_H1 },
        paragraph: { spacing: { before: 480, after: 200 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: FONT_HEADING, color: COLOR_H2 },
        paragraph: { spacing: { before: 360, after: 160 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, font: FONT_HEADING, color: COLOR_H3 },
        paragraph: { spacing: { before: 280, after: 120 }, outlineLevel: 2 } },
    ],
  },
  sections: [
    // 封面
    {
      properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 0, bottom: 0, left: 0, right: 0 } } },
      children: [
        new Table({
          width: { size: 11906, type: WidthType.DXA }, columnWidths: [11906],
          rows: [new TableRow({ height: { value: 4200, rule: "exact" }, children: [new TableCell({
            borders: noBorders, width: { size: 11906, type: WidthType.DXA },
            shading: { fill: COLOR_H1, type: ShadingType.CLEAR }, verticalAlign: "center",
            children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [
              new TextRun({ text: "智能健康手环", font: FONT_HEADING, size: 56, bold: true, color: "ffffff" }),
            ] })],
          })] })],
        }),
        new Paragraph({ spacing: { before: 1200, after: 200 }, alignment: AlignmentType.CENTER,
          children: [new TextRun({ text: "完 整 技 术 文 档", font: FONT_HEADING, size: 40, color: COLOR_H2 })] }),
        new Paragraph({ spacing: { before: 400 }, alignment: AlignmentType.CENTER,
          children: [new TextRun({ text: "基于 STM32F103 + FreeRTOS", font: FONT_BODY, size: 24, color: "7f8c8d" })] }),
        new Paragraph({ spacing: { before: 80 }, alignment: AlignmentType.CENTER,
          children: [new TextRun({ text: "MAX30102 · MPU6050 · DS18B20 · DS1302 · SSD1306", font: FONT_BODY, size: 22, color: "95a5a6" })] }),
        new Paragraph({ spacing: { before: 2000 }, alignment: AlignmentType.CENTER,
          children: [new TextRun({ text: "2026 年 6 月", font: FONT_BODY, size: 24, color: "95a5a6" })] }),
        new Paragraph({ spacing: { before: 800 }, children: [] }),
        new Table({
          width: { size: 11906, type: WidthType.DXA }, columnWidths: [11906],
          rows: [new TableRow({ height: { value: 700, rule: "exact" }, children: [new TableCell({
            borders: noBorders, width: { size: 11906, type: WidthType.DXA },
            shading: { fill: COLOR_H1, type: ShadingType.CLEAR }, verticalAlign: "center",
            children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [
              new TextRun({ text: "从零开始，读懂每一行代码", font: FONT_BODY, size: 22, color: "ffffff" }),
            ] })],
          })] })],
        }),
      ],
    },
    // 正文
    {
      properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1440, bottom: 1440, left: 1440, right: 1440 } } },
      headers: {
        default: new Header({ children: [new Paragraph({
          alignment: AlignmentType.RIGHT,
          border: { bottom: { style: BorderStyle.SINGLE, size: 1, color: "b0b0b0", space: 4 } },
          children: [new TextRun({ text: "智能健康手环 · 技术文档", font: FONT_BODY, size: 16, color: "95a5a6" })],
        })] }),
      },
      footers: {
        default: new Footer({ children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          children: [
            new TextRun({ text: "— ", font: FONT_BODY, size: 16, color: "95a5a6" }),
            new TextRun({ children: [PageNumber.CURRENT], font: FONT_BODY, size: 16, color: "95a5a6" }),
            new TextRun({ text: " —", font: FONT_BODY, size: 16, color: "95a5a6" }),
          ],
        })] }),
      },
      children,
    },
  ],
});

Packer.toBuffer(doc).then(buffer => {
  const out = "项目技术文档.docx";
  fs.writeFileSync(out, buffer);
  console.log(`✅ 已生成 ${out}（${(buffer.length / 1024).toFixed(1)} KB）`);
}).catch(e => { console.error("生成失败:", e); process.exit(1); });

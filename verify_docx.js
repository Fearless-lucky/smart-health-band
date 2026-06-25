const fs = require("fs");
const file = "项目技术文档.docx";
if (!fs.existsSync(file)) { console.error("文件不存在"); process.exit(1); }

// .docx 是 ZIP，直接用 Buffer 读 XML（跳过 ZIP 头，不完美但够统计）
const buf = fs.readFileSync(file);
const text = buf.toString("utf-8");
console.log(`文件大小: ${(buf.length / 1024).toFixed(1)} KB`);
console.log(`可读文本长度: ${text.length} 字符`);
console.log(`段落(w:p): ${(text.match(/<w:p[ >]/g) || []).length}`);
console.log(`表格(w:tbl): ${(text.match(/<w:tbl[ >]/g) || []).length}`);
console.log(`标题样式(Heading): ${(text.match(/Heading[123]/g) || []).length}`);
console.log(`粗体(w:b): ${(text.match(/<w:b\/>/g) || []).length}`);
console.log(`代码字体(Consolas): ${(text.match(/Consolas/g) || []).length}`);
console.log(`文字量(w:t): ${(text.match(/<w:t[ >]/g) || []).length} 个标签`);


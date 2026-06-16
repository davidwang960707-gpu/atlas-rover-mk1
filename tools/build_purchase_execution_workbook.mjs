import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(scriptDir, "..");
const csvPath = path.join(root, "Atlas_Rover_Mk1_制造图纸包_V1.0", "采购清单_Atlas_Rover_Mk1_V1.0.csv");
const outputPath = path.join(root, "采购执行表.xlsx");
const packOutputPath = path.join(root, "Atlas_Rover_Mk1_制造图纸包_V1.0", "采购执行表_Atlas_Rover_Mk1_V1.0.xlsx");

function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let inQuotes = false;
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const next = text[i + 1];
    if (inQuotes) {
      if (ch === '"' && next === '"') {
        field += '"';
        i += 1;
      } else if (ch === '"') {
        inQuotes = false;
      } else {
        field += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\n") {
      row.push(field);
      rows.push(row);
      row = [];
      field = "";
    } else if (ch !== "\r") {
      field += ch;
    }
  }
  if (field.length || row.length) {
    row.push(field);
    rows.push(row);
  }
  return rows;
}

const csvText = (await fs.readFile(csvPath, "utf8")).replace(/^\uFEFF/, "");
const [headers, ...records] = parseCsv(csvText);
const index = Object.fromEntries(headers.map((h, i) => [h, i]));

const rows = records.map((record) => [
  record[index["名称"]],
  record[index["数量"]],
  record[index["是否必需"]],
  record[index["推荐搜索词"]],
  record[index["备注"]],
]);

const linkTexts = records.map((record) => [
  record[index["京东"]],
  record[index["淘宝"]],
  record[index["拼多多"]],
]);

const workbook = Workbook.create();
const sheet = workbook.worksheets.add("采买执行");
sheet.showGridLines = false;

sheet.getRange("A1:H1").values = [["Atlas Rover Mk.1 V1.0 全量采购执行表"]];
sheet.getRange("A1:H1").merge();
sheet.getRange("A2:H2").values = [["来源：采购清单_Atlas_Rover_Mk1_V1.0.csv；包含已有、必买、推荐物料的京东/淘宝/拼多多搜索链接。"]];
sheet.getRange("A2:H2").merge();
sheet.getRange("A4:H4").values = [["项目", "数量", "优先级", "推荐搜索词", "下单校验", "京东", "淘宝", "拼多多"]];
sheet.getRange(`A5:E${rows.length + 4}`).values = rows;
sheet.getRange(`F5:H${rows.length + 4}`).values = linkTexts;

sheet.getRange("A1:H1").format = {
  fill: "#1F2937",
  font: { bold: true, color: "#FFFFFF", size: 16 },
};
sheet.getRange("A2:H2").format = {
  fill: "#F3F4F6",
  font: { color: "#374151", size: 10 },
};
sheet.getRange("A4:H4").format = {
  fill: "#B87333",
  font: { bold: true, color: "#FFFFFF" },
};
sheet.getRange(`A4:H${rows.length + 4}`).format = {
  borders: { preset: "all", style: "thin", color: "#D1D5DB" },
  wrapText: true,
  verticalAlignment: "top",
};
sheet.getRange(`A5:H${rows.length + 4}`).format = {
  fill: "#FFFBEB",
  font: { color: "#111827", size: 9 },
};
sheet.getRange("A:A").format.columnWidthPx = 220;
sheet.getRange("B:B").format.columnWidthPx = 60;
sheet.getRange("C:C").format.columnWidthPx = 70;
sheet.getRange("D:D").format.columnWidthPx = 260;
sheet.getRange("E:E").format.columnWidthPx = 430;
sheet.getRange("F:H").format.columnWidthPx = 310;
sheet.getRange("1:1").format.rowHeightPx = 30;
sheet.getRange("2:2").format.rowHeightPx = 26;
sheet.getRange(`5:${rows.length + 4}`).format.rowHeightPx = 56;
sheet.freezePanes.freezeRows(4);

const render = await workbook.render({
  sheetName: "采买执行",
  range: `A1:H${Math.min(rows.length + 4, 16)}`,
  scale: 1,
  format: "png",
});
await fs.mkdir(path.join(root, "tmp", "spreadsheets"), { recursive: true });
await fs.writeFile(path.join(root, "tmp", "spreadsheets", "purchase_execution_preview.png"), new Uint8Array(await render.arrayBuffer()));

const xlsx = await SpreadsheetFile.exportXlsx(workbook);
await xlsx.save(outputPath);
await xlsx.save(packOutputPath);

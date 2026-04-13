'use strict';
const fs = require('fs');
const path = require('path');

function findFile(candidates) {
  for (const f of candidates) {
    if (fs.existsSync(f)) return f;
  }
  return null;
}

const sdkRoot = path.join(__dirname, '..', '..');
const kwPath = findFile([
  path.join(sdkRoot, 'jas-compiler-c', 'src', 'keywords.c'),
  path.join(__dirname, '..', '..', '..', 'sdk-dependiente', 'jas-compiler-c', 'src', 'keywords.c'),
  path.join(__dirname, '..', '..', '..', 'sdk', 'jas-compiler-c', 'src', 'keywords.c'),
]);
const sisPath = findFile([
  path.join(sdkRoot, 'jas-compiler-c', 'src', 'sistema_llamadas.c'),
  path.join(__dirname, '..', '..', '..', 'sdk-dependiente', 'jas-compiler-c', 'src', 'sistema_llamadas.c'),
]);

if (!kwPath) throw new Error('No se encontro keywords.c (jas-compiler-c/src/keywords.c)');
if (!sisPath) throw new Error('No se encontro sistema_llamadas.c');

function parseStringArray(cSource, arrayName) {
  const re = new RegExp(`const char \\*const ${arrayName}\\[] = \\{([\\s\\S]*?)\\};`);
  const m = cSource.match(re);
  if (!m) throw new Error(`no ${arrayName} en ${arrayName === 'KEYWORDS' ? 'keywords.c' : 'sistema_llamadas.c'}`);
  return [...m[1].matchAll(/"([^"]+)"/g)].map((x) => x[1]);
}

const kwSrc = fs.readFileSync(kwPath, 'utf8');
const words = parseStringArray(kwSrc, 'KEYWORDS');
const forbiddenWords = parseStringArray(kwSrc, 'FORBIDDEN_ENGLISH');

const sisSrc = fs.readFileSync(sisPath, 'utf8');
const sistema = parseStringArray(sisSrc, 'SISTEMA_LLAMADAS');

const wordSet = new Set(words);
for (const w of sistema) {
  if (!wordSet.has(w)) {
    words.push(w);
    wordSet.add(w);
  }
}

const control = new Set([
  'principal', 'fin_principal', 'funcion', 'fin_funcion',
  'mientras', 'fin_mientras', 'cuando', 'fin_cuando',
  'si', 'sino', 'fin_si', 'retornar', 'romper', 'continuar',
  'hacer', 'fin_hacer', 'seleccionar', 'caso', 'defecto', 'fin_seleccionar',
  'intentar', 'atrapar', 'final', 'fin_intentar', 'lanzar',
  'registro', 'fin_registro', 'concepto', 'fin_concepto', 'macro', 'llamar', 'fin_archivo',
  'para_cada', 'fin_para_cada', 'sobre',
]);
const storage = new Set([
  'entero', 'texto', 'flotante', 'caracter', 'constante', 'u32', 'u64', 'u8', 'byte',
  'vec2', 'vec3', 'vec4', 'mat4', 'mat3', 'bool', 'lista', 'mapa',
]);
const wordOp = new Set([
  'con', 'valor', 'peso', 'igual', 'es', 'entrada', 'entonces', 'retorna',
  'como', 'o', 'y', 'no', 'mayor', 'menor', 'distinto', 'que', 'de', 'a',
]);
const imports = new Set(['usar', 'enviar', 'todo', 'todas']);
const constant = new Set(['verdadero', 'falso']);
const io = new Set([
  'imprimir', 'imprimir_sin_salto', 'imprimir_texto', 'ingresar_texto',
  'ingreso_inmediato', 'limpiar_consola', 'imprimir_flotante', 'imprimir_id',
  'decimal',
]);

const g1 = [], g2 = [], g3 = [], g4 = [], g5 = [], g6 = [], g7 = [];
for (const w of words) {
  if (control.has(w)) g1.push(w);
  else if (storage.has(w)) g2.push(w);
  else if (wordOp.has(w)) g3.push(w);
  else if (imports.has(w)) g4.push(w);
  else if (constant.has(w)) g5.push(w);
  else if (io.has(w)) g6.push(w);
  else g7.push(w);
}

function alt(a) {
  return a.slice().sort((x, y) => y.length - x.length).join('|');
}

function chunk(arr, maxLen) {
  const out = [];
  let cur = [];
  let len = 0;
  for (const w of arr.sort((x, y) => y.length - x.length)) {
    const add = w.length + (cur.length ? 1 : 0);
    if (len + add > maxLen && cur.length) {
      out.push(cur);
      cur = [];
      len = 0;
    }
    cur.push(w);
    len += add;
  }
  if (cur.length) out.push(cur);
  return out;
}

const MAX = 1800;
const chunks = chunk(g7, MAX).map(alt);

const out = {
  control: alt(g1),
  storage: alt(g2),
  wordOp: alt(g3),
  imports: alt(g4),
  constant: alt(g5),
  io: alt(g6),
  supportChunks: chunks,
  forbidden: alt(forbiddenWords.sort((x, y) => y.length - x.length)),
};

fs.writeFileSync(path.join(__dirname, 'keywords-groups.json'), JSON.stringify(out, null, 0), 'utf8');
console.log('wrote keywords-groups.json', {
  keywordsC: kwPath,
  sistemaC: sisPath,
  totalKeywords: words.length,
  supportChunkCount: chunks.length,
});

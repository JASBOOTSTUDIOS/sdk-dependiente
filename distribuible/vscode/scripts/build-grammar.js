'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const groups = JSON.parse(fs.readFileSync(path.join(__dirname, 'keywords-groups.json'), 'utf8'));

const forbidden = groups.forbidden || 'if|else|for|while|function|return|endif|endwhile|endfor|true|false|null|var|let|const|class|import|export';

function pat(match, name) {
  return { match, name };
}

function beginEnd(begin, end, name, patterns, beginCaptures, endCaptures) {
  const o = { begin, end, patterns: patterns || [], name };
  if (beginCaptures) o.beginCaptures = beginCaptures;
  if (endCaptures) o.endCaptures = endCaptures;
  return o;
}

function supportPatternList() {
  const chunks = groups.supportChunks && groups.supportChunks.length ? groups.supportChunks : [''];
  return chunks.map((chunk, i) => {
    const key = `keywords_support_${i + 1}`;
    if (!chunk) return { key, rule: pat('(?!)', 'support.function.builtin.jasboot') };
    return { key, rule: pat(`\\b(${chunk})\\b`, 'support.function.builtin.jasboot') };
  });
}

const supportList = supportPatternList();

const grammar = {
  $schema: 'https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json',
  name: 'jasboot',
  scopeName: 'source.jasboot',
  patterns: [{ include: '#jasboot_root' }],
  repository: {
    jasboot_root: {
      patterns: [
        { include: '#comments' },
        { include: '#strings_double' },
        { include: '#strings_backtick' },
        { include: '#strings_concept' },
        { include: '#numbers' },
        { include: '#operators' },
        { include: '#keywords_control' },
        { include: '#keywords_declarative' },
        { include: '#keywords_storage' },
        { include: '#keywords_word_op' },
        { include: '#keywords_import' },
        { include: '#keywords_constant' },
        { include: '#keywords_io' },
        ...supportList.map((s) => ({ include: `#${s.key}` })),
        { include: '#forbidden_english' },
        { include: '#identifiers' },
      ],
    },

    comments: {
      patterns: [
        { name: 'comment.line.double-slash.jasboot', match: '//.*' },
        { name: 'comment.line.number-sign.jasboot', match: '#.*' },
        beginEnd('/\\*', '\\*/', 'comment.block.jasboot', []),
      ],
    },

    strings_double: beginEnd('"', '"', 'string.quoted.double.jasboot', [
      { include: '#string_escapes' },
      { include: '#string_color_escape' },
      { include: '#interpolation' },
    ]),

    strings_backtick: beginEnd('`', '`', 'string.quoted.backtick.jasboot', [
      { include: '#string_escapes' },
      { include: '#string_color_escape' },
      { include: '#interpolation' },
    ]),

    string_escapes: {
      patterns: [
        {
          name: 'constant.character.escape.jasboot',
          match: '\\\\([nrt"\\\\]|[eE])',
        },
        {
          name: 'constant.character.escape.jasboot',
          match: '\\\\.',
        },
      ],
    },

    string_color_escape: {
      patterns: [
        {
          name: 'constant.other.escape.color.jasboot',
          match:
            '\\\\(normal|negrita|amarillo|magenta|blanco|verde|reset|rojo|verd|amar|azul|mage|cian|blan|negr|norm|rese)\\b',
        },
      ],
    },

    interpolation: beginEnd(
      '\\$\\{',
      '\\}',
      'meta.interpolation.jasboot',
      [{ include: '#interpolated_expr' }],
      { 0: { name: 'punctuation.definition.template-expression.begin.jasboot' } },
      { 0: { name: 'punctuation.definition.template-expression.end.jasboot' } }
    ),

    interpolated_expr: {
      patterns: [
        beginEnd('\\{', '\\}', 'meta.block.interpolation.jasboot', [{ include: '#interpolated_expr' }]),
        beginEnd('"', '"', 'string.quoted.double.jasboot', [
          { include: '#string_escapes' },
          { include: '#string_color_escape' },
        ]),
        beginEnd('`', '`', 'string.quoted.backtick.jasboot', [
          { include: '#string_escapes' },
          { include: '#string_color_escape' },
          { include: '#interpolation' },
        ]),
        { include: '#comments' },
        { include: '#numbers' },
        { include: '#operators' },
        { include: '#keywords_control' },
        { include: '#keywords_declarative' },
        { include: '#keywords_storage' },
        { include: '#keywords_word_op' },
        { include: '#keywords_import' },
        { include: '#keywords_constant' },
        { include: '#keywords_io' },
        ...supportList.map((s) => ({ include: `#${s.key}` })),
        { name: 'meta.brace.round.jasboot', match: '[()]' },
        { name: 'punctuation.separator.comma.jasboot', match: ',' },
        { name: 'variable.other.readwrite.jasboot', match: '\\b[a-zA-Z_][a-zA-Z0-9_]*\\b' },
      ],
    },

    strings_concept: beginEnd("'", "'", 'string.quoted.single.concept.jasboot', [
      { name: 'constant.character.escape.jasboot', match: '\\\\.' },
    ]),

    numbers: {
      patterns: [
        { name: 'constant.numeric.hex.jasboot', match: '\\b0[xX][0-9a-fA-F]+\\b' },
        { name: 'constant.numeric.float.jasboot', match: '\\b\\d+\\.\\d+\\b' },
        { name: 'constant.numeric.integer.jasboot', match: '\\b\\d+\\b' },
      ],
    },

    operators: {
      patterns: [
        { name: 'keyword.operator.arrow.jasboot', match: '=>' },
        { name: 'keyword.operator.comparison.jasboot', match: '(===|==|!=|<=|>=|\\+\\+|--|\\+=|-=|\\*=|/=|<<|>>)' },
        { name: 'keyword.operator.assignment.jasboot', match: '=' },
        { name: 'keyword.operator.jasboot', match: '[+\\-*/%?:!]' },
        { name: 'keyword.operator.jasboot', match: '[<>=]' },
        { name: 'punctuation.accessor.jasboot', match: '\\.' },
        { name: 'punctuation.definition.bracket.jasboot', match: '[()\\[\\]{}]' },
        { name: 'punctuation.separator.comma.jasboot', match: ',' },
        { name: 'punctuation.terminator.jasboot', match: ';' },
      ],
    },

    keywords_control: pat(`\\b(${groups.control})\\b`, 'keyword.control.jasboot'),
    keywords_declarative: pat(`\\b(${groups.declarative || 'app|vista|componente|tema|rutas|columna|fila|tarjeta|texto|titulo|subtitulo|boton_ruta|boton_alerta|boton_secundario|jasb'})\\b`, 'keyword.declarative.jasboot'),
    keywords_storage: pat(`\\b(${groups.storage})\\b`, 'storage.type.jasboot'),
    keywords_word_op: pat(`\\b(${groups.wordOp})\\b`, 'keyword.operator.word.jasboot'),
    keywords_import: pat(`\\b(${groups.imports})\\b`, 'keyword.control.import.jasboot'),
    keywords_constant: pat('\\b(verdadero|falso)\\b', 'constant.language.jasboot'),
    keywords_io: pat(`\\b(${groups.io})\\b`, 'support.function.builtin.io.jasboot'),

    forbidden_english: pat(`\\b(${forbidden})\\b`, 'invalid.illegal.name.jasboot'),

    identifiers: pat('\\b[a-zA-Z_][a-zA-Z0-9_]*\\b', 'variable.other.readwrite.jasboot'),
  },
};

for (const s of supportList) {
  grammar.repository[s.key] = s.rule;
}

const out = path.join(root, 'syntaxes', 'jasboot.tmLanguage.json');
fs.writeFileSync(out, JSON.stringify(grammar, null, 4) + '\n', 'utf8');
console.log('Wrote', out, 'support rules:', supportList.length);

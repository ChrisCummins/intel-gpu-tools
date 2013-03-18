/* Global Constants ********************************************************
 *
 *   The more capital letters in a variable name, the more important it is...
 */
var REG_FILE_SIZE = 128;
var REG_SIZE = 32; /* In bytes. */

var MAX_SUBREGNUM = Math.pow(2, 16);
var MAX_REG_SPAN = 2; /* The number of register a region can span.*/

/* Default values for regions. */
var DEFAULT_REG_FILE = 'g';
var DEFAULT_EXEC_SIZE = 8;
var DEFAULT_DATA_SIZE = 4; /* Float */

/* Vertical grid size. */
var ROWS = 10;

/* Our drawing context. */
var canvas = $('#canvas')[0];
var ctx = canvas.getContext('2d');

/* x,y offset to start drawing grid (leaves room for labels). */
var START_Y = 30;
var START_X = 10;

var COLOR_GOOD = '#dff0d8';
var COLOR_BAD = '#f2dede';

/* Derived. Don't touch. */
var COL_W = Math.floor((canvas.width - START_X) / REG_SIZE) - 1;
var ROW_H = Math.floor((canvas.height - START_Y) / ROWS);

/* This regexp will match the syntax of any direct addressing register address
 * region description. Note that matching this regexp only ensure that the
 * syntax is correct, there may still be non-syntactic errors in the
 * description, e.g., an invalid combination of strides, etc. */
var REGION_REGEXP = new RegExp(['^\s*(g|r)?0*',
                                '([0-9]?[0-9]|1[0-1][0-9]|12[0-7])',
                                '(\\.0*([0-9]?[0-9]?[0-9]|',
                                '[0-5][0-9]{3}|6[0-4][0-9]{3}|',
                                '65[0-4][0-9]{2}|',
                                '655[0-2][0-9]|6553[0-6]))?',
                                '(\s*<((0|1|2|4|8|16|32)[;,]\s*',
                                '(1|2|4|8|16),\*)?(0|1|2|4)>)?',
                                '(:?(ub|b|uw|w|ud|d|f|v))?$'].join(''), 'i');

/* This exception is thrown by an invalid region. */
function ParseError(title, msg) {
  this.title = title;
  this.msg = msg;
}

/* Dynamic Content *********************************************************
 *
 *   JQuery snippets and miscellaneous getters/setters for bringing our HTML
 *   elements to life.
 */

/* This function is a callback for a change in the page's hash. */
$(window).hashchange(function() {
  hideAlerts();

  /* Enable the 'Share' button. */
  $('#share-btn').removeAttr('disabled');

  try {
    decodeHash();
    submitRegion();
    drawRegion();
  } catch (e) {
    if (e instanceof ParseError) {
      showAlertError(e.title, e.msg);
      submitEmptyRegion();
      flashRegionFormBackground(COLOR_BAD);
    } else
      showAlertError('Application Error',
                     'Uncaught JavaScript exception.');
  }
});

$(function() {
  $('#exec-size-ul li a').click(function() {
    hideAlerts();
    $('#exec-size-btn:first-child').html($(this).text() +
                                         ' <span class="caret"></span>');
    $('#exec-size-btn:first-child').val($(this).text());

    if (isValidRegion())
      encodeHash();
  });
});

/* Region input. */
$('#region-form').keyup(function(event) {
  var key = event.keyCode;

  /* We want to update the region if we're typing something useful. */
  if (!(key >= 37 && key <= 40) &&
      (key == 13 || isValidRegion())) {
    encodeHash();
  }
});

/* --advanced. */
$('#advanced').mouseup(function() {
  toggleAdvanced();
});

var advanced = false;

function toggleAdvanced() {
  hideAlerts();
  advanced = !advanced;

  encodeHash();
}

/* Share */
function shareBtnCb(id) {
  toggleShare();
}

$('#share .close').click(function() {
  $('#share').hide();
});

/* Alerts */
$('#alert-error .close').click(function() {
  $('#alert-error').hide();
});

$('#alert .close').click(function() {
  $('#alert').hide();
});

function showAlertError(title, msg) {
  $('#alert-error-placeholder').html('<strong>' + title + '</strong> ' + msg);
  $('#alert-error').show();
}

function showAlert(title, msg) {
  $('#alert-placeholder').html('<strong>' + title + '</strong> ' + msg);
  $('#alert').show();
}

function toggleShare() {
  if ($('#share').is(':visible')) {
    $('#share').hide();
  } else if (location.hash) {
    /* Show the hash and highlight it for quick copying. */
    var hash = $('#share-hash')[0];

    hash.value = location.hash;
    $('#share').show();
    hash.focus();
    hash.select();
    hash.setSelectionRange(0, 120);
  }
}

function hideAlerts() {
  $('.alert').hide();
}

function setDefaultExecSize() {
  $('#exec-size-btn:first-child').text($('#default-exec-size-li').text());
}

function setCanvasCaption(msg) {
  $('#canvas-caption').text(msg);
}

function flashRegionFormBackground(color) {
  var regionForm = $('#region-form')[0];

  regionForm.style.background = color;
  var int = setInterval(function() {
    regionForm.style.background = '#FFF';
    clearInterval(int);
  }, 500);
}

function getRegionString() {
  return $('#region-form')[0].value;
}

function setRegionString(string) {
  $('#region-form')[0].value = string;
}

function clearRegionString(string) {
  setRegionString('');

  return false;
}

function getExecSize() {
  return parseInt($('#exec-size-btn:first-child')[0].text);
}

function setExecSize(size) {
  $('#exec-size-btn:first-child').html(size + ' <span class="caret"></span>');
  $('#exec-size-btn:first-child').val(size);
}

function bumpExecSizeUp() {
  if (getExecSize() <= 16)
    setExecSize(getExecSize() * 2);
  else
    setExecSize(1);

  if (getRegionString().match(REGION_REGEXP))
    encodeHash();

  return false;
}

/* Set a new hash based off of the regions. */
function encodeHash() {
  var regionString = getRegionString();
  var hash;

  if (regionString.match(/^\s*#/)) {
    /* If our region string is a hash, then use that. */
    hash = regionString;
  } else {
    hash = 'execsize=' + getExecSize() + '&region=' + regionString +
           '&advanced=' + ((advanced) ? 1 : 0);
  }

  location.hash = hash;
  $(window).hashchange();
}

/* Decode a region from URL hash. If the hash contains a malformed region,
 * show an error.
 *
 * @return 1 if hash contained region description, else 0.
 */
function decodeHash() {
  var args = location.hash.split('&');
  var argc = 0;

  for (var i = 0; i < args.length; i++) {
    var pair = args[i].split('=');

    switch (String(pair[0])) {
      case '#execsize':
        setExecSize(pair[1]);
        argc++;
        break;
      case 'region':
        /* We only set the region text if it does not match the current
         * text. This prevents the caret from being moved to the end of the
         * string while typing. */
        if (getRegionString() != pair[1])
          setRegionString(pair[1]);

        argc++;
        break;
      case 'advanced':
        var _advanced = (parseInt(pair[1]) ? true : false);
        advanced = _advanced;
        $('#advanced').prop('checked', _advanced);
        argc++;
        break;
    }
  }

  if (argc === 3) {
    /* We got all our arguments. */
    return 1;
  } else {
    if (argc) {
      /* We got some arguments, but not all. */
      showAlertError('Malformed URL', 'URL hash contained invalid ' +
                     'region description.');
      submitEmptyRegion();
    }

    return 0;
  }
}

/* Clear the canvas. */
function clearCanvas() {
  ctx.fillStyle = '#FFF';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
}

/* Draw the vertical column lines. */
function drawGridColumns() {
  var col_x = START_X;

  ctx.strokeStyle = '#c0c0c0';
  ctx.lineWidth = 1;

  for (var i = 0; i <= REG_SIZE; i++) {
    ctx.moveTo(col_x + .5, START_Y);
    ctx.lineTo(col_x + .5, START_Y + (ROWS * ROW_H));
    ctx.stroke();
    col_x += COL_W;
  }
}

/* Draw the column labels (byte indexes). */
function drawColLabels() {
  var col_x = START_X + 5;

  ctx.textAlign = 'center';
  ctx.fillStyle = '#000';
  ctx.font = '14px arial';

  for (var i = 0; i < REG_SIZE; i++) {
    ctx.fillText(REG_SIZE - 1 - i, col_x + COL_W / 2 - 5, START_Y - 5);
    col_x += COL_W;
  }
}

/* Draw the horizontal row lines. */
function drawGridRows() {
  var row_y = START_Y;

  ctx.strokeStyle = '#c0c0c0';
  ctx.lineWidth = 1;

  for (var i = 0; i <= ROWS; i++) {
    ctx.moveTo(START_X, row_y + .5);
    ctx.lineTo(START_X + (REG_SIZE * COL_W), row_y + .5);
    ctx.stroke();
    row_y += ROW_H;
  }
}

/* Draw the row labels (register numbers).
 *
 * @param regFile       The (optional) one or more character string that
 *                      prefixes a register number, for example 'g' for GRF.
 * @param startRegister The number to begin labeling registers from, for example
 *                      110.
 */
function drawRowLabels(regFile, startRegister) {
  var row_y = START_Y + ROW_H - (canvas.height / 30);

  ctx.textAlign = 'left';
  ctx.fillStyle = '#000';
  ctx.font = 'bold 14px arial';

  for (var i = 0; i < ROWS; i++) {
    ctx.fillText(regFile + (startRegister + i), canvas.width - 48, row_y);
    row_y += ROW_H;
  }
}

/* Draw a cell at the given coordinates.
 *
 * @param label Any text to place inside the cell.
 * @param x Cell origin (leftmost edge, in px).
 * @param y Cell origin (uppermost edge, in px).
 * @param width The width of the cell (in px).
 */
function drawCell(label, x, y, width, color) {
  ctx.fillStyle = color;
  ctx.globalAlpha = 0.7;
  ctx.fillRect(x, y, width, ROW_H);

  ctx.strokeStyle = '#06266F';
  ctx.lineWidth = 2;
  ctx.strokeRect(x + .5, y + .5, width + .5, ROW_H + .5);

  ctx.fillStyle = '#FFF';
  ctx.globalAlpha = 1.0;
  ctx.fillText(label, x + (width / 2), y + (ROW_H / 2) + 5);
}

/* Bootstrap. */
function init() {
  $('#share-btn').attr('disabled', 'disabled');

  if (decodeHash()) {
    $(window).hashchange();
  } else {
    hideAlerts();
    submitEmptyRegion();
  }
}

/* Key Bindings ************************************************************
 *
 *   Set document-wide key bindings.
 */
function pageBind(key, func) {
  $(document).bind('keydown', key, func);
  $('#region-form').bind('keydown', key, func);
  $('#share-hash').bind('keydown', key, func);
}

pageBind('a', toggleAdvanced);
pageBind('e', bumpExecSizeUp);
pageBind('s', toggleShare);
pageBind('esc', hideAlerts);
pageBind('c', clearRegionString);

$(document).bind('keydown', 'r', function() {
  $('#region-form').focus();
  return false;
});

$('#share-hash').bind('keydown', 'r', function() {
  $('#region-form').focus();
  return false;
});

$(document).bind('keydown', 'return', function() {
  if (isNaN(getExecSize()))
    setExecSize(DEFAULT_EXEC_SIZE);

  encodeHash();
});

/* Register Region parsing *************************************************
 *
 *   Our main program logic, which is responsible for parsing input strings
 *   and presenting the results.
 */
var execSize;
var regFile;
var register;
var subRegNum;
var vertStride;
var width;
var horzStride;
var dataSize;

/* Perform a dry-run parse of the region description.
 *
 * @return 1 if all inputs are valid, else 0.
 */
function isValidRegion() {
  if (getRegionString().match(REGION_REGEXP)) {
    /* It passed the regexp match, so lets see if we can submit it (without
     * drawing anything. */
    try {
      submitRegion();
      return 1;
    } catch (e) {
      return 0;
    }
  }

  return 0;
}

/* Gets the region description, execSize and --advanced. If any of the input
 * data is invalid, it will throw a descriptive ParseError exception.
 *
 * Region descriptions take the form:
 *    [RegFile] RegNum[.SubRegNum][<[VertStride;Width,]HorzStide>][:type]
 *
 * Values in square brackets ([x]) denote optional additions. Where optional
 * parts are omitted, the following assumptions are made:
 *
 *   [RegFile]                       RegFile is 'g'.
 *   [.SubRegNum]                    SubRegNum is 0.
 *   [VertSride;Width,]              Region is 1D.
 *   [<VertStride;Width,HorzStride>] Region is scalar.
 *   [:type]                         Type is float (dataSize = 4).
 */
function submitRegion() {
  var regionString = getRegionString();

  if (!regionString) {
    throw new ParseError('Syntax Error',
                         'Region must be in the form: ' +
                         '<code>RegNum.SubRegNum&lt;VertStride;' +
                         'Width,HorzStide&gt;:type</code>');
  }

  /* We don't want whitespace getting in our way, so lets remove it. */
  while (regionString.match(/\s/))
    regionString = regionString.replace(/\s+/, '');

  if (regionString.match(/:$/)) {
    throw new ParseError('Syntax Error',
                         'Illegal trailing <code>:</code> character.');
  }

  /* Strip the data type from the region string and set the dataSize, or use
   * default. */
  dataSize = regionString.match(/[a-z]+$/i);
  if (dataSize) {
    switch (String(dataSize).toLowerCase()) {
      case 'b':
      case 'ub':
        dataSize = 1;
        break;
      case 'uw':
      case 'w':
        dataSize = 2;
        break;
      case 'ud':
      case 'd':
      case 'f':
      case 'v':
        dataSize = 4;
        break;
      default:
        throw new ParseError('Illegal Data Type \'' + dataSize + '\'',
                             'Possible values: ' +
                             '<code>ub|b|uw|w|ud|d|f|v</code>.');
    }
    /* Strip the data type from the region string. */
    regionString = regionString.replace(/[a-z]+$/i, '');
  } else {
    dataSize = DEFAULT_DATA_SIZE;
  }

  /* Test and set the [optional] RegFile. */
  regFile = regionString.match(/^[a-z]+/i);
  if (regFile) {
    /* We have a regFile, so strip it from the region string. */
    regionString = regionString.replace(/^[a-z]+/i, '');
  } else {
    /* No RegFile, use the default. */
    regFile = DEFAULT_REG_FILE;
  }

  /* Strip the register number from the region string. */
  register = parseInt(regionString.match(/^\d+/));
  regionString = regionString.replace(/^\d+/g, '');

  /* Get or set the number of execution channels. */
  execSize = getExecSize();
  if (isNaN(execSize)) {
    /* ExecSize not set, show warning and set it to the default value. */
    execSize = DEFAULT_EXEC_SIZE;
    showAlert('Warning', 'Execution size not set, using default value ' +
              'of \'8\'.');
    setDefaultExecSize();
  }

  /* Strip the the SubRegNum from the region string, or use default. Here, we
   * deliberately catch a wider set of values then are permissible, so that in
   * the case of region descriptions containing syntax errors such as
   * '10.foo<1>', we still receive the 'foo' as a subRegNum, which we can then
   * use to give a more informative syntax error. */
  if (regionString.match(/^\.\w+/)) {
    regionString = regionString.replace(/^\./, '');
    subRegNum = parseInt(regionString.match(/^\w+/));
    regionString = regionString.replace(/^\w+/, '');
  } else {
    subRegNum = 0;
  }

  if (isNaN(subRegNum)) {
    throw new ParseError('Invalid SubRegNum', 'A subregister must be an ' +
                         'integer value in the range [0, ' +
                         MAX_SUBREGNUM + '].');
  }

  if (subRegNum > MAX_SUBREGNUM) {
    throw new ParseError('Invalid SubRegNum \'' + subRegNum + '\'',
                         'A subregister must be an integer value in the ' +
                         'range [0, ' + MAX_SUBREGNUM + '].');
  }

  /* Convert subRegNum to a byte offset if in --advanced mode. */
  if (advanced)
    subRegNum *= dataSize;

  /* Let's do a sanity check to make sure the region description is in the
   * correct form. */
  if (!regionString.match(/^(<[\d;,]+>)?:?$/g)) {
    throw new ParseError('Syntax Error', 'Region descriptions must be in ' +
                         'the form: <code>&lt;VertStride;Width,' +
                         'HorzStride&gt;</code>');
  }

  /* Let's discard those pesky angle brackets now, and the trailing ':', if
   * present. */
  if (regionString) {
    regionString = regionString.replace(/^</, '');
    regionString = regionString.replace(/>?:?$/, '');
  }

  /* Direct addressing region descriptions can take 3 forms:
   *
   *    Region   ::= <VertStride> “;” <Width> “,” <HorzStride>
   *                 | <VertStride> “,” <Width> “,” <HorzStride>
   *    RegionV  ::= <VertStride>
   *    RegionE  ::= ""
   *
   * Here is where we'll extract the vertStride, width and horzStride from
   * this description, inserting defaults if any of the values are missing.
   * If the region does not match any of those forms, it is a syntax error.
   */
  if (regionString.match(/^\d+[;,]\d+,\d+$/)) {
    /* Region   ::= <VertStride> “;” <Width> “,” <HorzStride>
     *              | <VertStride> “,” <Width> “,” <HorzStride> */
    vertStride = parseInt(regionString.match(/^\d+/));
    regionString = regionString.replace(/^\d+[;,]/, '');
    width = parseInt(regionString.match(/^\d+/));
    regionString = regionString.replace(/^\d+,/, '');
    horzStride = parseInt(regionString);
  } else if (regionString.match(/^\d+$/)) {
    /* RegionV ::= <VertStride> */
    vertStride = parseInt(regionString);
    width = 1;
    horzStride = 0;
  } else if (regionString.match(/^$/)) {
    /* RegionE ::= "" */
    vertStride = 0;
    width = 1;
    horzStride = 0;
  } else {
    throw new ParseError('Syntax Error', 'Region descriptions must be in ' +
                         'the form: <code>&lt;VertStride;Width,' +
                         'HorzStride&gt;</code>');
  }

  /* Now that the input string is parsed, we perform some checks for general
   * restrictions on regioning parameters. */
  if (isNaN(register)) {
    throw new ParseError('Syntax Error', 'RegNum must be an integer in ' +
                         'the range [0, 127].');
  }

  if (register >= REG_FILE_SIZE) {
    throw new ParseError('Out of Bounds', 'RegNum \'' + register +
                         '\' must be an integer in the range [0, 127].');
  }

  if (!String(vertStride).match(/^(0|1|2|4|8|16|32)$/)) {
    throw new ParseError('Invalid Vertical Stride \'' + vertStride + '\'',
                         'Possible values: <code>0|1|2|4|8|16|32</code>.');
  }

  if (!String(width).match(/^(1|2|4|8|16)$/)) {
    throw new ParseError('Invalid Width \'' + width + '\'',
                         'Possible values: <code>1|2|4|8|16</code>.');
  }

  if (!String(horzStride).match(/^(0|1|2|4)$/)) {
    throw new ParseError('Invalid Horizontal Stride \'' + horzStride + '\'',
                         'Possible values: <code>0|1|2|4</code>.');
  }

  if (execSize < width) {
    throw new ParseError('Out of Bounds', 'Execution size must be ' +
                         'greater or equal to width. (execSize: \'' +
                         execSize + '\', width: \'' + width + '\')');
  }

  if (execSize == width && horzStride && vertStride != width * horzStride) {
    throw new ParseError('Invalid Region', 'If execution size is equal ' +
                         'to width and horizontal stride is greater ' +
                         'than 0, vertical stride must be set to the ' +
                         'product of the width and horizontal strides. ' +
                         '(execSize: width: \'' + width + '\', ' +
                         'horzStride: \'' + horzStride +
                         '\', vertStride: \'' + vertStride + '\')');
  }

  if (width == 1 && horzStride) {
    throw new ParseError('Invalid Region', 'If width is equal to \'1\', ' +
                         'horizontal stride must be \'0\'. ' +
                         '(width: \'' + width + '\', ' +
                         'horzStride: \'' + horzStride + '\')');
  }

  if (execSize == 1 && width == 1 &&
      (vertStride || horzStride)) {
    throw new ParseError('Invalid Scalar', 'If both the execution size ' +
                         'and width are equal to \'1\', both vertical ' +
                         'stride and horizontal stride must be \'0\'. ' +
                         '(execSize: \'' + execSize + '\', ' +
                         'width: \'' + width + '\', ' +
                         'vertStride: \'' + vertStride +
                         '\', horzStride: \'' + horzStride + '\')');
  }

  if (!vertStride && !horzStride && width != 1) {
    throw new ParseError('Invalid Region', 'If both vertical and ' +
                         'horizontal strides are equal to \'0\', ' +
                         'width must be set to \'1\'. ' +
                         '(vertStride: \'' + vertStride + '\', ' +
                         'horzStride: \'' + horzStride + '\', ' +
                         'width: \'' + width + '\')');
  }
}

function submitEmptyRegion() {
  /* Zero the region description. */
  execSize = 0;
  regFile = 'g';
  register = 0;
  subRegNum = 0;
  vertStride = 0;
  width = 0;
  horzStride = 0;
  dataSize = 0;

  drawRegion();
  setCanvasCaption('');
}

function drawRegion() {
  var startRegister; /* The lowermost register to draw. */

  /* Set the caption. */
  var description = execSize + ' channels executing on a region starting at ' +
                    'register ' + regFile + register + '.' + subRegNum +
                    ', with a width of ' + width +
                    ', a horizontal stride of ' + horzStride +
                    ' and a vertical stride of ' + vertStride + '.';

  setCanvasCaption(description);

  if (execSize)
    flashRegionFormBackground(COLOR_GOOD);

  startRegister = Math.max(register - 2, 0);
  startRegister = Math.min(startRegister, REG_FILE_SIZE - ROWS);

  clearCanvas();
  drawGridColumns();
  drawGridRows();
  drawColLabels();
  drawRowLabels(regFile, startRegister);
  drawRegionCells(startRegister);
}

/* Performs the meat of the region viewing work.
 *
 * @param startRegister The first register drawn by drawRowLabels().
 */
function drawRegionCells(startRegister) {
  var count = 0;
  var rows = execSize / width;
  var firstReg;

  ctx.textAlign = 'center';
  ctx.font = 'bold 14px arial';

  for (var r = 0; r < rows; r++) {
    for (var c = 0; c < width; c++) {
      /* The absolute offset (in bytes) of the target element from the
       * starting register. */
      var regByte = subRegNum + (r * vertStride * dataSize) +
                    (c * horzStride * dataSize);

      /* The register of the current element. */
      var reg = register + Math.floor(regByte / REG_SIZE);
      var regOffset = (regByte % REG_SIZE);

      /* We cache the first reg so that we can detect when a region spans
       * more than 2 registers. */
      if (!r && !c)
        firstReg = reg;

      /* We only show a warning for out of range accesses. */
      if (reg - firstReg >= MAX_REG_SPAN) {
        flashRegionFormBackground(COLOR_BAD);
        showAlert('Out of Bounds', 'A source cannot span more ' +
                  'than 2 adjacent GRF registers ' +
                  '(affected region is shown in red).');
      }

      /* We check now that everything is in bounds. If not, alert the user
       * and cancel the drawing. */
      if (reg >= REG_FILE_SIZE) {
        showAlertError('Out of Bounds', 'Register access \'' + reg +
                       '\' out of bounds!');
        flashRegionFormBackground(COLOR_BAD);
      }

      /* Now lets get the canvas coordinates of the cell for drawing. */
      var x = START_X + (REG_SIZE - regOffset - dataSize) * COL_W;
      var y = START_Y + ((reg - startRegister) * ROW_H);

      /* Finally, we can paint the cell, assuming of course that we are
       * painting a cell within the execution size. Otherwise, we are
       * done. This check is needed for cases where:
       *          (execSize % width) != 0. */
      if (count < execSize && reg < REG_FILE_SIZE) {
        /* We colour out-of-range cells red. */
        var color = (reg - firstReg < MAX_REG_SPAN) ?
                    '#0f4fa8' : '#e27171';
        var cw = dataSize * COL_W;

        if (x < START_X) {
          var overlap = START_X - x;
          cw -= overlap;
          color = '#e27171';
          drawCell(count, START_X + REG_SIZE * COL_W - overlap,
                   y + ROW_H, overlap, '#e27171');
          x = START_X;
        }

        drawCell(count++, x, y, cw, color);
      } else {
        return;
      }
    }
  }
}

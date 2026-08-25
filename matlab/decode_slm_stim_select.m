function [value, info] = decode_slm_stim_select(data, sampling_rate, varargin)
%DECODE_SLM_STIM_SELECT Decode an SLM_STIM_SELECT packet from a digital trace.
%
%   VALUE = DECODE_SLM_STIM_SELECT(DATA, SAMPLING_RATE) locates the first
%   packet in DATA and returns the transmitted stimulus index (0-255), or NaN
%   if no packet is found. DATA is a vector of binary samples of the
%   SLM_STIM_SELECT line acquired at SAMPLING_RATE hertz.
%
%   [VALUE, INFO] = DECODE_SLM_STIM_SELECT(...) also returns a struct with the
%   packet location, the individual bits, and quality checks (see below).
%
%   Wire format (see gather() in src/main.cpp)
%   -----------------------------------------
%   The line idles LOW. A transmission is:
%
%       reset pulse : HIGH for 10 ms
%       data        : 8 bits, MSB first, 1 ms per bit
%       idle        : LOW again
%
%   Total 18 ms. The Teensy drives the line from its 1 kHz gather() tick, so
%   both durations are exact multiples of that tick.
%
%   Detection strategy
%   ------------------
%   The reset pulse cannot be identified by pulse width, because bit 7 follows
%   it with no intervening edge: a byte of 255 is one continuous 18 ms HIGH
%   run, and any byte with bit 7 set merges the framing with the first data
%   bit. Instead the decoder keys on the rising edge out of idle, then samples
%   each bit at a fixed offset from that edge. Candidate edges are accepted
%   only if the HIGH run that follows is long enough to be a reset pulse; this
%   is what rejects an edge into a data bit, which can be HIGH for at most
%   (NUMBITS-1) * BITMS = 7 ms (bits 6..0), and rejects short glitches.
%
%   Because a LOW->HIGH transition is required, DATA must contain at least one
%   idle-LOW sample before the packet. A trace that begins part way into a
%   reset pulse is not decodable and is reported as not found.
%
%   Each bit is read by majority vote over the middle SAMPLEFRACTION of its
%   1 ms window, which tolerates edge jitter and sample-clock offset without
%   assuming an integer number of samples per bit.
%
%   Name-value options
%   ------------------
%   'ResetMs'        Reset pulse length, ms.                 Default 10
%   'BitMs'          Bit period, ms.                         Default 1
%   'NumBits'        Bits per packet.                        Default 8
%   'BitOrder'       'msb' or 'lsb' first.                   Default 'msb'
%   'Tolerance'      Fractional slack on the accepted reset
%                    pulse length.                           Default 0.2
%   'Threshold'      Samples above this count as HIGH.       Default 0.5
%   'SampleFraction' Fraction of each bit window used for
%                    the majority vote, centred in the
%                    window.                                 Default 0.6
%   'StartIndex'     First sample to search from.            Default 1
%
%   INFO fields
%   -----------
%   found         True if a packet was decoded.
%   value         Same as VALUE.
%   bits          1-by-NUMBITS logical, in transmission order (bits(1) is the
%                 first bit on the wire, i.e. the MSB with default BitOrder).
%   start_index   Index of the first HIGH sample of the reset pulse.
%   start_time    (start_index - 1) / SAMPLING_RATE, seconds, so that a trace
%                 starting at t = 0 gives the packet onset directly.
%   end_index     Index of the last sample of the final bit window.
%   next_index    Index to pass as 'StartIndex' to find the following packet.
%   reset_len_ms  Measured length of the leading HIGH run. Equals ResetMs when
%                 bit 7 is 0, and extends into the data bits when it is 1, so
%                 this is a framing diagnostic, not a bit value.
%   bit_margin    Worst-case per-bit vote agreement, 0.5 (ambiguous) to 1
%                 (every sample in the window agreed). Below ~0.9 suggests a
%                 timing or sample-rate mismatch.
%   stop_low      True if the line was LOW for the tick after the last bit, as
%                 the firmware guarantees. NaN if the trace ended first. A
%                 false here means the packet framing is suspect.
%   message       Human-readable status.
%
%   Example
%   -------
%       [v, info] = decode_slm_stim_select(trace, 20000);
%       if info.found
%           fprintf('stimulus %d at t = %.4f s\n', v, info.start_time);
%       end
%
%   Finding every packet in a long trace:
%
%       idx = 1; values = [];
%       while true
%           [v, info] = decode_slm_stim_select(trace, fs, 'StartIndex', idx);
%           if ~info.found, break; end
%           values(end+1) = v;         %#ok<AGROW>
%           idx = info.next_index;
%       end

% -------------------------------------------------------------------------
% Input handling
% -------------------------------------------------------------------------
p = inputParser;
p.FunctionName = mfilename;
addRequired(p, 'data', @(x) (isnumeric(x) || islogical(x)) && isvector(x) && ~isempty(x));
addRequired(p, 'sampling_rate', @(x) isnumeric(x) && isscalar(x) && isfinite(x) && x > 0);
addParameter(p, 'ResetMs', 10, @(x) isnumeric(x) && isscalar(x) && x > 0);
addParameter(p, 'BitMs', 1, @(x) isnumeric(x) && isscalar(x) && x > 0);
addParameter(p, 'NumBits', 8, @(x) isnumeric(x) && isscalar(x) && x >= 1 && mod(x, 1) == 0);
addParameter(p, 'BitOrder', 'msb', @(x) any(strcmpi(x, {'msb', 'lsb'})));
addParameter(p, 'Tolerance', 0.2, @(x) isnumeric(x) && isscalar(x) && x >= 0 && x < 1);
addParameter(p, 'Threshold', 0.5, @(x) isnumeric(x) && isscalar(x) && isfinite(x));
addParameter(p, 'SampleFraction', 0.6, @(x) isnumeric(x) && isscalar(x) && x > 0 && x <= 1);
addParameter(p, 'StartIndex', 1, @(x) isnumeric(x) && isscalar(x) && x >= 1);
parse(p, data, sampling_rate, varargin{:});
opt = p.Results;

n_bit = opt.BitMs * 1e-3 * sampling_rate;      % samples per data bit
n_reset = opt.ResetMs * 1e-3 * sampling_rate;  % samples in the reset pulse

if n_bit < 2
    error('decode_slm_stim_select:sampleRateTooLow', ...
        ['Sampling rate %g Hz gives only %.2f samples per %g ms bit. ' ...
         'At least 2 are needed; >= 10 kHz is expected.'], ...
        sampling_rate, n_bit, opt.BitMs);
end

% The acceptance floor for a reset pulse must sit above the longest HIGH run
% that a run of data bits alone can produce, otherwise a trace that starts
% part way into a packet could latch onto a data bit as if it were framing.
reset_min = n_reset * (1 - opt.Tolerance);
data_run_max = (opt.NumBits - 1) * n_bit;
if reset_min <= data_run_max
    error('decode_slm_stim_select:ambiguousTolerance', ...
        ['Tolerance %.3f puts the minimum accepted reset pulse at %.2f ms, ' ...
         'which a run of %d data bits (%.2f ms) could imitate. Use a ' ...
         'tolerance below %.3f.'], ...
        opt.Tolerance, reset_min / (1e-3 * sampling_rate), opt.NumBits - 1, ...
        data_run_max / (1e-3 * sampling_rate), ...
        1 - (opt.NumBits - 1) * opt.BitMs / opt.ResetMs);
end
% A whole packet stays HIGH throughout when every bit is 1.
reset_max = (n_reset + opt.NumBits * n_bit) * (1 + opt.Tolerance);

level = data(:) > opt.Threshold;   % logical column, orientation-independent
n = numel(level);

value = NaN;
info = struct('found', false, 'value', NaN, 'bits', [], ...
    'start_index', NaN, 'start_time', NaN, 'end_index', NaN, ...
    'next_index', NaN, 'reset_len_ms', NaN, 'bit_margin', NaN, ...
    'stop_low', NaN, 'message', 'no packet found');

% -------------------------------------------------------------------------
% Find the first rising edge that can start a reset pulse
% -------------------------------------------------------------------------
first = max(2, ceil(opt.StartIndex));  % need level(k-1) to see an edge
candidates = find(level(first:n) & ~level(first-1:n-1)) + first - 1;

if isempty(candidates)
    if opt.StartIndex <= 1 && level(1)
        info.message = ['trace starts HIGH with no preceding idle sample; ' ...
                        'a packet already in progress cannot be framed'];
    else
        info.message = 'no rising edge found';
    end
    return;
end

for r = candidates(:)'
    % Length of the HIGH run beginning at this edge.
    fall = find(~level(r:n), 1);
    if isempty(fall)
        % Run continues past the end of the trace: nothing after it to
        % decode, and no later edge can exist either.
        info.message = sprintf(...
            'trace ends during a HIGH run starting at sample %d', r);
        return;
    end
    run_len = fall - 1;   % samples HIGH, from r inclusive

    if run_len < reset_min || run_len > reset_max
        continue;  % a data bit, a glitch, or not this protocol
    end

    % -----------------------------------------------------------------
    % Framing accepted. Sample the bit windows.
    % -----------------------------------------------------------------
    % The transition sits between samples r-1 and r, so place the packet
    % origin half a sample before the first HIGH sample.
    t0 = r - 0.5;
    last_needed = t0 + n_reset + opt.NumBits * n_bit;
    if last_needed > n
        info.message = sprintf(...
            ['packet at sample %d is truncated: needs %d samples, ' ...
             'trace has %d'], r, ceil(last_needed), n);
        return;
    end

    bits = false(1, opt.NumBits);
    margins = zeros(1, opt.NumBits);
    for k = 0:(opt.NumBits - 1)
        [bits(k + 1), margins(k + 1)] = ...
            voteWindow(level, t0 + n_reset + k * n_bit, n_bit, opt.SampleFraction);
    end

    % The firmware returns the line to idle for the tick after the last bit.
    [stop_high, ~] = voteWindow(level, t0 + n_reset + opt.NumBits * n_bit, ...
        n_bit, opt.SampleFraction);
    stop_window_end = t0 + n_reset + (opt.NumBits + 1) * n_bit;
    if stop_window_end > n
        stop_low = NaN;   % trace ended before the guard tick
    else
        stop_low = ~stop_high;
    end

    % -----------------------------------------------------------------
    % Assemble the byte
    % -----------------------------------------------------------------
    if strcmpi(opt.BitOrder, 'msb')
        weights = 2 .^ (opt.NumBits - 1:-1:0);
    else
        weights = 2 .^ (0:opt.NumBits - 1);
    end
    value = sum(double(bits) .* weights);

    info.found = true;
    info.value = value;
    info.bits = bits;
    info.start_index = r;
    info.start_time = (r - 1) / sampling_rate;
    info.end_index = floor(t0 + n_reset + opt.NumBits * n_bit);
    info.next_index = info.end_index + 1;
    info.reset_len_ms = run_len / (1e-3 * sampling_rate);
    info.bit_margin = min(margins);
    info.stop_low = stop_low;

    if isnan(stop_low)
        info.message = 'decoded; trace ended before the trailing idle tick';
    elseif ~stop_low
        info.message = ['decoded, but the line was not idle after the last ' ...
                        'bit; framing is suspect'];
    elseif info.bit_margin < 0.9
        info.message = sprintf(...
            'decoded, but bit vote margin is only %.2f; check timing', ...
            info.bit_margin);
    else
        info.message = 'ok';
    end
    return;
end

info.message = sprintf(...
    ['found %d rising edge(s), none with a HIGH run of %.2f-%.2f ms ' ...
     '(reset pulse)'], numel(candidates), ...
    reset_min / (1e-3 * sampling_rate), reset_max / (1e-3 * sampling_rate));
end


function [bit, margin] = voteWindow(level, win_start, n_bit, fraction)
%VOTEWINDOW Majority vote over the centre FRACTION of one bit window.
%   WIN_START and N_BIT are in (possibly fractional) sample units. MARGIN is
%   the fraction of sampled points that agreed with the outcome: 1 means
%   unanimous, 0.5 means an even split.
n = numel(level);
centre = win_start + n_bit / 2;
half = fraction * n_bit / 2;

i1 = max(1, ceil(centre - half));
i2 = min(n, floor(centre + half));
if i2 < i1
    % Window narrower than the sample spacing: fall back to the nearest
    % single sample to the window centre.
    i1 = min(n, max(1, round(centre)));
    i2 = i1;
end

frac_high = mean(level(i1:i2));
bit = frac_high >= 0.5;
if bit
    margin = frac_high;
else
    margin = 1 - frac_high;
end
end

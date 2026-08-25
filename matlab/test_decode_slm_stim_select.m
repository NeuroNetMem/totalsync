function test_decode_slm_stim_select()
%TEST_DECODE_SLM_STIM_SELECT Self-test for decode_slm_stim_select.
%
%   Synthesises SLM_STIM_SELECT traces the way gather() in src/main.cpp drives
%   the line, then checks that every byte round-trips. Run with no arguments;
%   errors on the first failure and prints a summary otherwise.

fprintf('test_decode_slm_stim_select\n');

% ---------------------------------------------------------------------
% All 256 values round-trip, at several sampling rates. 10 kHz and 20 kHz
% divide the 1 ms bit period evenly; 13.7 kHz and 44.1 kHz do not, which is
% what exercises the fractional-window sampling.
% ---------------------------------------------------------------------
rates = [10000, 13700, 20000, 44100];
for fs = rates
    for v = 0:255
        trace = make_trace(v, fs, 'LeadMs', 25, 'TrailMs', 25);
        [got, info] = decode_slm_stim_select(trace, fs);
        assert(info.found, 'fs=%g v=%d: not found (%s)', fs, v, info.message);
        assert(got == v, 'fs=%g: sent %d, decoded %d', fs, v, got);
        assert(isequal(info.stop_low, true), ...
            'fs=%g v=%d: trailing idle not seen', fs, v);
        assert(info.bit_margin > 0.5, 'fs=%g v=%d: margin %.3f', ...
            fs, v, info.bit_margin);
    end
    fprintf('  %5g Hz: all 256 values round-trip\n', fs);
end

fs = 20000;

% ---------------------------------------------------------------------
% Packet onset is reported at the first HIGH sample
% ---------------------------------------------------------------------
lead_ms = 30;
trace = make_trace(170, fs, 'LeadMs', lead_ms, 'TrailMs', 20);
[~, info] = decode_slm_stim_select(trace, fs);
expected = lead_ms * 1e-3 * fs + 1;
assert(info.start_index == expected, 'start_index %d, expected %d', ...
    info.start_index, expected);
assert(abs(info.start_time - lead_ms * 1e-3) < 1 / fs, 'start_time wrong');
fprintf('  onset located at the first HIGH sample\n');

% ---------------------------------------------------------------------
% reset_len_ms reflects that bit 7 merges with the reset pulse
% ---------------------------------------------------------------------
[~, info] = decode_slm_stim_select(make_trace(0, fs), fs);
assert(abs(info.reset_len_ms - 10) < 0.1, 'value 0: reset run %.2f ms', ...
    info.reset_len_ms);
[~, info] = decode_slm_stim_select(make_trace(255, fs), fs);
assert(abs(info.reset_len_ms - 18) < 0.1, 'value 255: reset run %.2f ms', ...
    info.reset_len_ms);
[~, info] = decode_slm_stim_select(make_trace(128, fs), fs);
assert(abs(info.reset_len_ms - 11) < 0.1, 'value 128: reset run %.2f ms', ...
    info.reset_len_ms);
fprintf('  all-ones (18 ms) and all-zeros (10 ms) framing both decode\n');

% ---------------------------------------------------------------------
% MSB first: 1 is a single bit at the end, 128 a single bit at the start
% ---------------------------------------------------------------------
[~, info] = decode_slm_stim_select(make_trace(1, fs), fs);
assert(isequal(info.bits, logical([0 0 0 0 0 0 0 1])), 'bit order for 1');
[~, info] = decode_slm_stim_select(make_trace(128, fs), fs);
assert(isequal(info.bits, logical([1 0 0 0 0 0 0 0])), 'bit order for 128');
fprintf('  bits are reported MSB first\n');

% ---------------------------------------------------------------------
% Only the first packet is decoded, and next_index walks the rest
% ---------------------------------------------------------------------
sent = [7, 200, 33];
multi = [];
for v = sent
    multi = [multi; make_trace(v, fs, 'LeadMs', 40, 'TrailMs', 5)]; %#ok<AGROW>
end
first = decode_slm_stim_select(multi, fs);
assert(first == sent(1), 'first packet: got %d', first);

idx = 1; found = [];
while true
    [v, i2] = decode_slm_stim_select(multi, fs, 'StartIndex', idx);
    if ~i2.found, break; end
    found(end + 1) = v; %#ok<AGROW>
    idx = i2.next_index;
end
assert(isequal(found, sent), 'walk found [%s], expected [%s]', ...
    num2str(found), num2str(sent));
fprintf('  sequential packets walk correctly via next_index\n');

% ---------------------------------------------------------------------
% Rejections and edge cases
% ---------------------------------------------------------------------
% Idle line only.
[v, info] = decode_slm_stim_select(zeros(1000, 1), fs);
assert(isnan(v) && ~info.found, 'idle trace should not decode');

% Short glitches are not reset pulses.
glitchy = zeros(2000, 1);
glitchy(500:502) = 1;
glitchy(900:905) = 1;
[~, info] = decode_slm_stim_select(glitchy, fs);
assert(~info.found, 'glitches should be rejected');

% A trace that begins part way into a reset pulse must not be framed off a
% later data bit. Value 127 gives reset HIGH, bit7 LOW, then 7 HIGH bits.
full = make_trace(127, fs, 'LeadMs', 20, 'TrailMs', 20);
cut = full(round(20e-3 * fs) + round(5e-3 * fs):end);  % start mid reset pulse
[~, info] = decode_slm_stim_select(cut, fs);
assert(~info.found, ...
    'mid-pulse start should be rejected, got %d (%s)', info.value, info.message);

% Truncated packet.
short = make_trace(99, fs, 'LeadMs', 10, 'TrailMs', 0);
short = short(1:end - round(3e-3 * fs));
[~, info] = decode_slm_stim_select(short, fs);
assert(~info.found, 'truncated packet should be rejected');

% Row vectors, logical input and a preceding unrelated pulse all work.
row = make_trace(42, fs, 'LeadMs', 15, 'TrailMs', 15)';
assert(decode_slm_stim_select(row, fs) == 42, 'row vector input');
assert(decode_slm_stim_select(logical(row), fs) == 42, 'logical input');
with_lead_pulse = [zeros(200, 1); ones(40, 1); zeros(200, 1); ...
                   make_trace(42, fs, 'LeadMs', 5, 'TrailMs', 15)];
assert(decode_slm_stim_select(with_lead_pulse, fs) == 42, ...
    'unrelated 2 ms pulse before the packet');
fprintf('  rejects idle, glitches, mid-pulse starts and truncated packets\n');

% A tolerance that would make a data-bit run look like framing is an error.
try
    decode_slm_stim_select(make_trace(5, fs), fs, 'Tolerance', 0.35);
    error('expected an ambiguousTolerance error');
catch err
    assert(strcmp(err.identifier, 'decode_slm_stim_select:ambiguousTolerance'), ...
        'wrong error: %s', err.identifier);
end

% Sampling rate below 2 samples per bit is an error.
try
    decode_slm_stim_select(make_trace(5, fs), 1500);
    error('expected a sampleRateTooLow error');
catch err
    assert(strcmp(err.identifier, 'decode_slm_stim_select:sampleRateTooLow'), ...
        'wrong error: %s', err.identifier);
end
fprintf('  guards on tolerance and sampling rate fire\n');

% ---------------------------------------------------------------------
% Robustness: one sample of jitter on every edge
% ---------------------------------------------------------------------
for v = [0, 1, 85, 128, 170, 254, 255]
    trace = make_trace(v, fs, 'LeadMs', 20, 'TrailMs', 20, 'JitterSamples', 1);
    [got, info] = decode_slm_stim_select(trace, fs);
    assert(info.found && got == v, ...
        'jittered v=%d: got %d (%s)', v, info.value, info.message);
end
fprintf('  tolerates one sample of edge jitter\n');

fprintf('all tests passed\n');
end


function trace = make_trace(value, fs, varargin)
%MAKE_TRACE Synthesise the line as gather() drives it: 10 ms HIGH, then 8 bits
%   MSB first at 1 ms each, then idle LOW. Returns a column vector.
p = inputParser;
addParameter(p, 'LeadMs', 20);
addParameter(p, 'TrailMs', 20);
addParameter(p, 'ResetMs', 10);
addParameter(p, 'BitMs', 1);
addParameter(p, 'JitterSamples', 0);
parse(p, varargin{:});
o = p.Results;

bits = bitget(uint8(value), 8:-1:1);   % MSB first, matching the firmware

% Level for each tick of the transmission, then the segment lengths in
% samples with optional jitter on the boundaries.
levels = [ones(1, o.ResetMs / o.BitMs), double(bits)];
n_bit = o.BitMs * 1e-3 * fs;

lead = round(o.LeadMs * 1e-3 * fs);
trail = round(o.TrailMs * 1e-3 * fs);

trace = zeros(lead, 1);
edge = lead;                            % samples emitted so far
for k = 1:numel(levels)
    nominal = round(k * n_bit) + lead;
    if o.JitterSamples > 0 && k < numel(levels)
        nominal = nominal + randi([-o.JitterSamples, o.JitterSamples]);
    end
    count = max(1, nominal - edge);
    trace = [trace; repmat(levels(k), count, 1)]; %#ok<AGROW>
    edge = edge + count;
end
trace = [trace; zeros(trail, 1)];
end

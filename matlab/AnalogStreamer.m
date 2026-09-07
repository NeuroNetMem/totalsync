classdef AnalogStreamer < handle
  %UNTITLED Summary of this class goes here
  %   Detailed explanation goes here
  
  properties
        AO_channel = 11     % Analog output channels to send data to 
        channelRanges = [-5 5]
        sampleRate = 1E4      % Analog input sample Rate in Hz
        sampleWriteSize = 1000  % Read off this many samples then plot and log to disk
        dataType = 'int16'
        values = []
        last_value
        hTask
  end
  
  
  properties   % (Hidden)
        fid = -1
        fname = ''        
        buffer
        idx 
        data
  end
  
  
  
  methods
    function obj = AnalogStreamer(fname)
      %UNTITLED Construct an instance of this class
      %   Detailed explanation goes here
      obj.fname = fname;
      resourceStore = dabs.resources.ResourceStore(); % object that holds all system resources
      vDAQ = resourceStore.filterByName('vDAQ0');     % filter just the vDAQ for the tasks
      obj.hTask = dabs.vidrio.ddi.AoTask(vDAQ.hDevice,'Analog output task');
      obj.hTask.addChannel(obj.AO_channel);
      obj.hTask.sampleRate = obj.sampleRate;
     
      obj.hTask.sampleCallbackN = obj.sampleWriteSize;
      obj.hTask.sampleCallbackAutoRead = 1;
      obj.hTask.sampleCallback = @obj.writeData;
      obj.hTask.sampleMode = 'continuous';
      if ~isempty(obj.fname)
        load(obj.fname)
        obj.data = double(slm_select_10khz)';
        obj.idx = 1;
      end
    end
    
    function updateBuffer(obj)
      
      obj.buffer = 3.3 * obj.data(obj.idx:(obj.idx+obj.sampleWriteSize-1));
      obj.idx = obj.idx+obj.sampleWriteSize;
      if obj.idx > length(obj.data) - obj.sampleWriteSize - 1
        obj.idx = 1;
      end
      
    end
    
    function start(obj)
      
      if ~isempty(obj.fname)
        obj.updateBuffer()
      else
        obj.idx = -5;
        obj.buffer = ones(obj.sampleWriteSize, 1) * obj.idx;
      end
        
      obj.hTask.writeOutputBuffer(obj.buffer)
      obj.hTask.start();
    end
    
    function stop(obj)
     
      
      obj.hTask.stop();
    end
    
    function abort(obj)
       
      obj.hTask.abort()
    end
    
      
    
    function writeData(obj, ~, evt)
      % disp('in callbsck');
      if ~isempty(obj.fname)
        obj.updateBuffer();
      else
        obj.idx = obj.idx + 1;
        if obj.idx > 5
          obj.idx = -5;
        end
        obj.buffer = obj.idx * ones(obj.sampleWriteSize, 1);
      end
      
      % obj.hTask.stop()
      obj.hTask.writeOutputBuffer(obj.buffer)
      % obj.hTask.start()
    end
    
  end
        
     
    
  
end


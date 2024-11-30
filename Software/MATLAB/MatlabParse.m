% MATLAB Script: Plot CSV Data with Interactive Subplots
% Load CSV file
[file, path] = uigetfile('*.csv', 'Select a CSV file');
data = readtable(fullfile(path, file));

% Extract SampleIndex and Marker columns
sampleIndex = data.SampleIndex;
if any(strcmp(data.Properties.VariableNames, 'Markers'))
    markers = data.Markers;
else
    markers = [];
end

% Identify channel columns for 735 and 850 wavelengths
channelNames = data.Properties.VariableNames;
channel735 = contains(channelNames, '735');
channel850 = contains(channelNames, '850');
markers = contains(channelNames, 'MarkerLabel')

% Find unique channel identifiers (e.g., S1A0L)
channelIDs735 = extractAfter(channelNames(channel735), '735');
channelIDs850 = extractAfter(channelNames(channel850), '850');

% Find common channel IDs
commonIDs = intersect(channelIDs735, channelIDs850);

% Prepare for plotting
figure;
gridRows = 4; % Number of rows in the subplot grid
gridCols = 5; % Number of columns in the subplot grid
numPlots = numel(commonIDs);
subplots = gobjects(numPlots, 1); % Store subplot handles
positions = cell(numPlots, 1); % Store default positions

% Define the enlarged position for zoomed-in subplot
enlargedPos = [0.09, 0.1, 0.85, 0.85];

for i = 1:numPlots
    % Identify columns for current channel pair
    col735 = channel735 & contains(channelNames, commonIDs{i});
    col850 = channel850 & contains(channelNames, commonIDs{i});
    
    % Extract data
    data735 = data{:, col735};
    data850 = data{:, col850};
    markerLabel = data{:, 43}(find((data{:, 42} >= 0) ~= 0));
    markerData = data{:, 42}(find((data{:, 42} >= 0) ~= 0));
    
    % Plot in a subplot
    ax = subplot(gridRows, gridCols, i);
    subplots(i) = ax; % Store the subplot handle
    positions{i} = ax.Position; % Store the default position
    
    plot(sampleIndex, data735, 'b', 'DisplayName', ['735nm - ' commonIDs{i}]);
    hold on;
    grid on;
    plot(sampleIndex, data850, 'r', 'DisplayName', ['850nm - ' commonIDs{i}]);
    hold on;
    
    % Add markers if they exist
    if ~isempty(markerData)
        colors = lines(length(markerData)); 
        for j = 1:length(markerData)
            xline(markerData(j),'DisplayName', string(markerLabel(j)), 'Color', colors(j, :), 'LineWidth', 1.5)
        end
    end
    
    % Labels and legend
    xlabel('Sample Index');
    ylabel('Raw ADC Value');
    legend;
    title(['Channel ' commonIDs{i}]);
    hold off;
    
    % Set ButtonDownFcn for interactivity
    set(ax, 'ButtonDownFcn', {@subplotZoom, ax, positions{i}, enlargedPos});
end

% Subplot zoom function
function subplotZoom(~, ~, ax, originalPos, enlargedPos)
    if isequal(ax.Position, enlargedPos)
        ax.Position = originalPos; % Minimize back to original position
    else
        ax.Position = enlargedPos; % Enlarge to full figure size
        axes(ax); % Bring axes to focus
    end
end
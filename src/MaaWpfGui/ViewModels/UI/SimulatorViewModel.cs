// <copyright file="SimulatorViewModel.cs" company="MaaAssistantArknights">
// Part of the MaaWpfGui project, maintained by the MaaAssistantArknights team (Maa Team)
// Copyright (C) 2021-2025 MaaAssistantArknights Contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License v3.0 only as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY
// </copyright>

#nullable enable
using System;
using System.Windows;
using MaaWpfGui.Helper;
using Serilog;
using Stylet;

namespace MaaWpfGui.ViewModels.UI;

public class SimulatorViewModel : Screen
{
    private readonly ILogger _logger = Log.ForContext<SimulatorViewModel>();

    public SimulatorViewModel()
    {
        DisplayName = LocalizationHelper.GetString("Simulator");
    }

    private string _aiEndpoint = "http://localhost:8765";

    public string AiEndpoint
    {
        get => _aiEndpoint;
        set => SetAndNotify(ref _aiEndpoint, value);
    }

    private bool _aiEnabled;

    public bool AiEnabled
    {
        get => _aiEnabled;
        set
        {
            SetAndNotify(ref _aiEnabled, value);
            ApplyAiSetting();
        }
    }

    private string _aiStatus = "Not connected";

    public string AiStatus
    {
        get => _aiStatus;
        set => SetAndNotify(ref _aiStatus, value);
    }

    public void CheckAiConnection()
    {
        _logger.Information("Simulator: CheckAiConnection called");

        try
        {
            var proxy = Instances.AsstProxy;
            if (proxy == null)
            {
                AiStatus = "Error: AsstProxy is null";
                return;
            }

            bool connected = proxy.AsstIsAiConnected();
            AiStatus = connected ? "Connected" : "Not connected";
            _logger.Information("Simulator: AiStatus={Status}", AiStatus);

            MessageBox.Show(
                connected ? "AI server is reachable." : "Cannot reach AI server.\nCheck that server.py is running and the endpoint is correct.",
                "AI Connection Test",
                connected ? MessageBoxButton.OK : MessageBoxButton.OK,
                connected ? MessageBoxImage.Information : MessageBoxImage.Warning);
        }
        catch (Exception ex)
        {
            AiStatus = "Error: " + ex.Message;
            _logger.Error(ex, "Simulator: CheckAiConnection failed");
            MessageBox.Show("Error: " + ex.Message, "AI Connection Test", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void ApplyAiSetting()
    {
        try
        {
            var proxy = Instances.AsstProxy;
            if (proxy == null)
            {
                _logger.Warning("Simulator: AsstProxy is null, cannot apply");
                return;
            }

            if (_aiEnabled && !string.IsNullOrWhiteSpace(_aiEndpoint))
            {
                _logger.Information("Simulator: calling AsstSetAiEndpoint({Endpoint})", _aiEndpoint);
                proxy.AsstSetAiEndpoint(_aiEndpoint);
                _logger.Information("Simulator: AsstSetAiEndpoint returned");
                AiStatus = "Configured";
            }
            else
            {
                proxy.AsstSetAiEndpoint(string.Empty);
                AiStatus = "Disabled";
            }
        }
        catch (Exception ex)
        {
            AiStatus = "Error: " + ex.Message;
            _logger.Error(ex, "Simulator: ApplyAiSetting failed");
        }
    }
}

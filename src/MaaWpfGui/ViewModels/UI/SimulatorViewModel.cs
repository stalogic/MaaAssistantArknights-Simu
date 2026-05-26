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
using MaaWpfGui.Configuration.Single.MaaTask;
using MaaWpfGui.Constants;
using MaaWpfGui.Helper;
using MaaWpfGui.Main;
using MaaWpfGui.Models.AsstTasks;
using MaaWpfGui.Services;
using MaaWpfGui.ViewModels.UserControl.TaskQueue;
using Newtonsoft.Json.Linq;
using Serilog;
using static MaaWpfGui.Main.AsstProxy;
using Stylet;

namespace MaaWpfGui.ViewModels.UI;

public class SimulatorViewModel : Screen
{
    private readonly ILogger _logger = Log.ForContext<SimulatorViewModel>();

    public SimulatorViewModel()
    {
        DisplayName = LocalizationHelper.GetString("Simulator");
    }

    // --- AI Connection ---

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

    // --- AI Decision Switches ---

    private bool _aiRecruit;

    public bool AiRecruit
    {
        get => _aiRecruit;
        set => SetAndNotify(ref _aiRecruit, value);
    }

    private bool _aiBattle;

    public bool AiBattle
    {
        get => _aiBattle;
        set => SetAndNotify(ref _aiBattle, value);
    }

    private bool _aiShopping;

    public bool AiShopping
    {
        get => _aiShopping;
        set => SetAndNotify(ref _aiShopping, value);
    }

    private bool _aiEncounter;

    public bool AiEncounter
    {
        get => _aiEncounter;
        set => SetAndNotify(ref _aiEncounter, value);
    }

    private bool _aiRouting;

    public bool AiRouting
    {
        get => _aiRouting;
        set => SetAndNotify(ref _aiRouting, value);
    }

    // --- Actions ---

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
                MessageBoxButton.OK,
                connected ? MessageBoxImage.Information : MessageBoxImage.Warning);
        }
        catch (Exception ex)
        {
            AiStatus = "Error: " + ex.Message;
            _logger.Error(ex, "Simulator: CheckAiConnection failed");
            MessageBox.Show("Error: " + ex.Message, "AI Connection Test", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    public void StartRoguelike()
    {
        _logger.Information("Simulator: StartRoguelike called");

        try
        {
            var proxy = Instances.AsstProxy;
            if (proxy == null)
            {
                MessageBox.Show("AsstProxy is not initialized.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            var settings = RoguelikeSettingsUserControlModel.Instance;
            var task = new AsstRoguelikeTask
            {
                Theme = settings.RoguelikeTheme,
                Mode = settings.RoguelikeMode,
                Starts = settings.RoguelikeStartsCount,
                Difficulty = settings.RoguelikeDifficulty,
                Squad = settings.RoguelikeSquad,
                Roles = settings.RoguelikeRoles,
                CoreChar = settings.RoguelikeCoreChar,
                UseSupport = settings.RoguelikeUseSupportUnit,
                UseSupportNonFriend = settings.RoguelikeEnableNonfriendSupport,
                InvestmentEnabled = settings.RoguelikeInvestmentEnabled,
                InvestmentCount = settings.RoguelikeInvestmentEnabled ? settings.RoguelikeInvestsCount : int.MaxValue,
                InvestmentWithMoreScore = settings.RoguelikeInvestmentWithMoreScore,
                InvestmentStopWhenFull = settings.RoguelikeStopWhenInvestmentFull,
                CollectibleModeShopping = settings.RoguelikeCollectibleModeShopping,
                CollectibleModeSquad = settings.RoguelikeCollectibleModeSquad,
                StartWithEliteTwo = settings.RoguelikeStartWithEliteTwo,
                StartWithEliteTwoNonBattle = settings.RoguelikeOnlyStartWithEliteTwo,
                StopAtFinalBoss = settings.RoguelikeStopAtFinalBoss,
                StopAtMaxLevel = settings.RoguelikeStopAtMaxLevel,
                MonthlySquadAutoIterate = settings.RoguelikeMonthlySquadAutoIterate,
                MonthlySquadCheckComms = settings.RoguelikeMonthlySquadCheckComms,
                DeepExplorationAutoIterate = settings.RoguelikeDeepExplorationAutoIterate,
                FindPlaytimeTarget = settings.RoguelikeFindPlaytimeTarget,
                RefreshTraderWithDice = settings.RoguelikeRefreshTraderWithDiceRaw,
                StartWithSeed = settings.RoguelikeStartWithSeed ? settings.RoguelikeSeed : null,
            };

            var (_, taskParams) = task.Serialize();
            taskParams ??= new JObject();
            taskParams["ai_recruit"] = _aiRecruit;
            taskParams["ai_battle"] = _aiBattle;
            taskParams["ai_shopping"] = _aiShopping;
            taskParams["ai_encounter"] = _aiEncounter;
            taskParams["ai_routing"] = _aiRouting;

            bool ok = proxy.AsstAppendTaskWithEncoding(
                TaskType.Roguelike, AsstTaskType.Roguelike, taskParams);

            if (!ok)
            {
                MessageBox.Show("Failed to append Roguelike task.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            proxy.AsstStart();
            _logger.Information("Simulator: Roguelike started, AI: recruit={Recruit}, battle={Battle}, shop={Shop}, encounter={Encounter}, routing={Routing}",
                _aiRecruit, _aiBattle, _aiShopping, _aiEncounter, _aiRouting);

            AiStatus = "Roguelike started";
        }
        catch (Exception ex)
        {
            _logger.Error(ex, "Simulator: StartRoguelike failed");
            MessageBox.Show("Failed to start Roguelike: " + ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
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

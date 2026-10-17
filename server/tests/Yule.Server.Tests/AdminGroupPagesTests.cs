using System.Net;
using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using Yule.Server.Data;

namespace Yule.Server.Tests;

/// <summary>The Groups pages (spec <c>family-groups</c>, "Administration through the server").</summary>
public class AdminGroupPagesTests
{
    private const string G1 = "g-aaaaaaaa";
    private const string G2 = "g-bbbbbbbb";
    private const string Gran = "d-000000000001";
    private const string Grandpa = "d-000000000002";
    private const string Emma = "d-000000000003";

    private static async Task<AdminSite> SiteWithFamilyAsync()
    {
        var site = new AdminSite();
        await site.SeedGroupAsync(G1, "Brander Grandparents");
        await site.SeedGroupAsync(G2, "Smith Grandparents");
        await site.SeedDeviceAsync(Gran, "Gran", "stable", G1);
        await site.SeedDeviceAsync(Grandpa, "Grandpa", "stable", G1);
        await site.SeedDeviceAsync(Emma, "Emma");
        return site;
    }

    private static Task<List<string>> MembersOfAsync(AdminSite site, string groupId) =>
        site.WithDbAsync(async db => await db.Memberships.Where(m => m.GroupId == groupId).Select(m => m.DeviceId).OrderBy(d => d).ToListAsync());

    [Fact]
    public async Task Creating_a_group_gives_it_a_generated_id_and_opens_its_page()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.PostFormAsync("/groups", "/groups", ("Name", "  Brander Grandparents "));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        var group = await site.WithDbAsync(db => db.Groups.SingleAsync());
        Assert.Equal("Brander Grandparents", group.Name);
        Assert.True(Identifiers.IsValidGroupId(group.Id), group.Id);
        Assert.Equal($"/groups/{group.Id}", AdminClient.Location(response));
        var page = await client.GetStringAsync($"/groups/{group.Id}");
        Assert.Contains("Brander Grandparents", page);
        Assert.Contains("No members yet", page);
        Assert.Empty(site.Broker.Applied); // a group with nobody in it changes nothing on the broker
    }

    [Fact]
    public async Task Two_groups_may_have_the_same_name()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        await client.PostFormAsync("/groups", "/groups", ("Name", "Grandparents"));
        await client.PostFormAsync("/groups", "/groups", ("Name", "Grandparents"));

        var groups = await site.WithDbAsync(db => db.Groups.ToListAsync());
        Assert.Equal(2, groups.Count);
        Assert.Equal(2, groups.Select(g => g.Id).Distinct().Count());
    }

    [Theory]
    [InlineData("")]
    [InlineData("    ")]
    public async Task A_group_needs_a_name(string name)
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.PostFormAsync("/groups", "/groups", ("Name", name));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("Give the group a name", await response.Content.ReadAsStringAsync());
        Assert.Empty(await site.WithDbAsync(db => db.Groups.ToListAsync()));
    }

    [Fact]
    public async Task A_group_name_may_be_forty_characters_but_not_forty_one_counting_what_people_see()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        await client.PostFormAsync("/groups", "/groups", ("Name", new string('x', 40)));
        await client.PostFormAsync("/groups", "/groups", ("Name", new string('y', 41)));
        // Emoji are two UTF-16 characters each but one character to a person (and to the device).
        await client.PostFormAsync("/groups", "/groups", ("Name", string.Concat(Enumerable.Repeat("🎄", 40))));
        await client.PostFormAsync("/groups", "/groups", ("Name", string.Concat(Enumerable.Repeat("🎄", 41))));

        var names = (await site.WithDbAsync(db => db.Groups.ToListAsync())).Select(g => g.Name).ToList();
        Assert.Equal(2, names.Count);
        Assert.Contains(new string('x', 40), names);
        Assert.Contains(string.Concat(Enumerable.Repeat("🎄", 40)), names);
    }

    [Fact]
    public async Task The_list_shows_each_group_with_its_members()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync("/groups");

        Assert.Contains("Brander Grandparents", html);
        Assert.Contains("Gran, Grandpa", html);
        Assert.Contains("Smith Grandparents", html);
        Assert.Contains("no members", html);
    }

    [Fact]
    public async Task Adding_a_member_saves_it_and_tells_the_broker_about_that_device()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G2}";

        var response = await client.PostFormAsync(path + "?handler=AddMember", path, ("DeviceId", Emma));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal([Emma], await MembersOfAsync(site, G2));
        Assert.Equal([Emma], site.Broker.AppliedIds);
        var page = await client.GetStringAsync(path);
        Assert.Contains("Added to the group", page);
        Assert.Contains("Emma", page);
    }

    [Fact]
    public async Task Only_devices_that_are_not_yet_members_can_be_chosen_to_add()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync($"/groups/{G1}");
        var options = Regex.Matches(html, "<option value=\"(d-[0-9a-f]{12})\"").Select(m => m.Groups[1].Value).ToList();

        Assert.Equal([Emma], options);
    }

    [Fact]
    public async Task A_device_in_two_groups_is_a_member_of_both()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();

        foreach (var group in new[] { G1, G2 })
        {
            await client.PostFormAsync($"/groups/{group}?handler=AddMember", $"/groups/{group}", ("DeviceId", Emma));
        }

        Assert.Contains(Emma, await MembersOfAsync(site, G1));
        Assert.Contains(Emma, await MembersOfAsync(site, G2));
    }

    [Theory]
    [InlineData("d-ffffffffffff")]
    [InlineData("not-a-device")]
    [InlineData("")]
    public async Task Adding_a_device_that_is_not_registered_is_refused(string deviceId)
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G2}";

        var response = await client.PostFormAsync(path + "?handler=AddMember", path, ("DeviceId", deviceId));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("Choose one of the registered devices", await response.Content.ReadAsStringAsync());
        Assert.Empty(await MembersOfAsync(site, G2));
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task Adding_the_same_member_twice_leaves_one_membership()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G2}";

        await client.PostFormAsync(path + "?handler=AddMember", path, ("DeviceId", Emma));
        var again = await client.PostFormAsync(path + "?handler=AddMember", path, ("DeviceId", Emma));

        Assert.Equal(HttpStatusCode.Redirect, again.StatusCode);
        Assert.Equal([Emma], await MembersOfAsync(site, G2));
    }

    [Fact]
    public async Task Removing_a_member_takes_the_device_out_of_the_group_only()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G1}";

        var response = await client.PostFormAsync(path + "?handler=RemoveMember", path, ("DeviceId", Gran));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal([Grandpa], await MembersOfAsync(site, G1));
        Assert.Equal(3, (await site.WithDbAsync(db => db.Devices.ToListAsync())).Count);
        Assert.Equal(2, (await site.WithDbAsync(db => db.Groups.ToListAsync())).Count);
        Assert.Equal([Gran], site.Broker.AppliedIds);
    }

    [Fact]
    public async Task Renaming_a_group_updates_every_member_but_nobody_else()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G1}";

        var response = await client.PostFormAsync(path + "?handler=Rename", path, ("Name", "  The Branders  "));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("The Branders", (await site.WithDbAsync(db => db.Groups.SingleAsync(g => g.Id == G1))).Name);
        Assert.Equal("Smith Grandparents", (await site.WithDbAsync(db => db.Groups.SingleAsync(g => g.Id == G2))).Name);
        Assert.Equal([Gran, Grandpa], site.Broker.AppliedIds.Order(StringComparer.Ordinal));
    }

    [Fact]
    public async Task Renaming_to_an_invalid_name_changes_nothing()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G1}";

        var response = await client.PostFormAsync(path + "?handler=Rename", path, ("Name", "   "));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("Give the group a name", await response.Content.ReadAsStringAsync());
        Assert.Equal("Brander Grandparents", (await site.WithDbAsync(db => db.Groups.SingleAsync(g => g.Id == G1))).Name);
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task Renaming_to_the_same_name_does_not_touch_the_broker()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G1}";

        await client.PostFormAsync(path + "?handler=Rename", path, ("Name", "Brander Grandparents"));

        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task Deleting_asks_for_confirmation_first()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync($"/groups/{G1}/remove");

        Assert.Contains("Delete Brander Grandparents?", html);
        Assert.Contains("Gran, Grandpa", html);
        Assert.Equal(2, (await site.WithDbAsync(db => db.Groups.ToListAsync())).Count);
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task Deleting_a_group_frees_its_members_but_keeps_their_devices()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G1}/remove";

        var response = await client.PostFormAsync(path, path);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("/groups", AdminClient.Location(response));
        Assert.Equal([G2], (await site.WithDbAsync(db => db.Groups.ToListAsync())).Select(g => g.Id));
        Assert.Empty(await site.WithDbAsync(db => db.Memberships.ToListAsync()));
        Assert.Equal(3, (await site.WithDbAsync(db => db.Devices.ToListAsync())).Count);
        Assert.Equal([Gran, Grandpa], site.Broker.AppliedIds.Order(StringComparer.Ordinal));
        Assert.Contains("Deleted Brander Grandparents.", await client.GetStringAsync("/groups"));
    }

    [Fact]
    public async Task A_group_that_does_not_exist_is_not_found()
    {
        using var site = await SiteWithFamilyAsync();
        var client = await site.LoggedInClientAsync();

        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/groups/g-zzzzzzzz")).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/groups/g-zzzzzzzz/remove")).StatusCode);
        var add = await client.PostFormAsync("/groups/g-zzzzzzzz?handler=AddMember", $"/groups/{G1}", ("DeviceId", Emma));
        Assert.Equal(HttpStatusCode.NotFound, add.StatusCode);
        var delete = await client.PostFormAsync("/groups/g-zzzzzzzz/remove", $"/groups/{G1}/remove");
        Assert.Equal(HttpStatusCode.NotFound, delete.StatusCode);
        Assert.Empty(await MembersOfAsync(site, G2));
    }

    [Fact]
    public async Task When_the_broker_cannot_be_reached_the_membership_is_still_saved_and_the_administrator_is_told()
    {
        using var site = await SiteWithFamilyAsync();
        site.Broker.Behaviour = _ => throw new TimeoutException("The broker did not answer");
        var client = await site.LoggedInClientAsync();
        var path = $"/groups/{G2}";

        var response = await client.PostFormAsync(path + "?handler=AddMember", path, ("DeviceId", Emma));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal([Emma], await MembersOfAsync(site, G2));
        var page = await client.GetStringAsync(path);
        Assert.Contains("could not be updated just now", page);
        Assert.Contains("Emma", page);
    }
}

# Simple Redmine Notifier

Simple Remine Notifier runs in background, checks updated issues and sends a notifications.

## Example

![notification_example.png](doc\notification_example.png?raw=true)

## Settings

Open the `settings.ini` in any text editor. File content looks like this:

```ini
[main]
api_key=0
server=http://demo.redmine.org
interval=60
track_assigned_to_me=true
track_my=true
track_watched=true
```

Set your user API key and Redmine server. You can change remain options (update interval in seconds and tracked issue types) as you want.

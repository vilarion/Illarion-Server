import xml.etree.ElementTree as ET
import requests
import os

annotations = []
errors = 0

root = ET.parse('/tmp/cppcheck.xml').getroot()
for error_tag in root.findall('errors/error'):
    errors = errors + 1

    for location_tag in error_tag.findall('location'):
        annotations.append({
                            'path': location_tag.get('file'),
                            'start_line': int(location_tag.get('line')),
                            'end_line': int(location_tag.get('line')),
                            'annotation_level': 'failure',
                            'message': error_tag.get('verbose'),
                            'title': error_tag.get('msg') + ' [' + error_tag.get('id') + ']'
                          })
if errors > 0:
    payload = {'output': {'title': 'Cppcheck Results', 'summary': str(errors) + ' errors detected.', 'annotations': annotations}}

    repo = os.environ['GITHUB_REPOSITORY']
    job_id = os.environ['JOB_ID']
    run_id = os.environ['GITHUB_RUN_ID']
    github_token = os.environ['GITHUB_TOKEN']

    headers = {'Accept': 'application/vnd.github.v3+json',
               'Authorization': 'token ' + github_token}
    url = 'https://api.github.com/repos/' + repo + '/actions/runs/' + str(run_id)
    r = requests.get(url, headers=headers)
    check_suite_url = r.json()['check_suite_url']

    url = check_suite_url + '/check-runs'
    r = requests.get(url, headers=headers)
    check_run = next((item for item in r.json()['check_runs'] if item['name'] == job_id), None)

    url = 'https://api.github.com/repos/' + repo + '/check-runs/' + str(check_run['id'])
    r = requests.patch(url, headers=headers, json=payload)
    print(r.status_code)

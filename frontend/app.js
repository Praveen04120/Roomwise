/**
 * Roomwise — Empty Classroom Finder
 * Public Portal JavaScript Client
 */

(function () {
  'use strict';

  const API_BASE = (window.ROOMWISE_API_URL || '').replace(/\/+$/, '');
  function apiUrl(path) {
    return `${API_BASE}${path}`;
  }

  const RoomwiseApp = {
    currentFloor: 1,
    floors: [],
    settings: null,
    selectedDate: '',

    init: async function () {
      this.initDate();
      this.startClock();
      this.bindGlobalEvents();
      await this.loadSettings();
      this.alignInitialSearchWindow();
      await this.loadSummary();
      await this.loadFloors();
      const firstFloor = this.floors.length > 0 ? this.floors[0].floor_number : 1;
      await this.selectFloor(firstFloor);
      await this.executeSearch();
    },

    bindGlobalEvents: function () {
      const floorSelect = document.getElementById('searchFloor');
      if (floorSelect) {
        floorSelect.addEventListener('change', () => {
          this.executeSearch();
        });
      }

      document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
          this.closeModal();
          this.closeMobileMenu();
        }
      });
    },

    toggleMobileMenu: function () {
      const drawer = document.getElementById('mobileNavDrawer');
      const btn = document.getElementById('mobileMenuBtn');
      if (!drawer) return;
      const isOpen = drawer.classList.toggle('open');
      if (btn) btn.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
    },

    closeMobileMenu: function () {
      const drawer = document.getElementById('mobileNavDrawer');
      const btn = document.getElementById('mobileMenuBtn');
      if (drawer) drawer.classList.remove('open');
      if (btn) btn.setAttribute('aria-expanded', 'false');
    },

    // Format Date YYYY-MM-DD
    initDate: function () {
      const today = new Date();
      const yyyy = today.getFullYear();
      const mm = String(today.getMonth() + 1).padStart(2, '0');
      const dd = String(today.getDate()).padStart(2, '0');
      this.selectedDate = `${yyyy}-${mm}-${dd}`;

      const dateInput = document.getElementById('searchDate');
      if (dateInput) {
        dateInput.value = this.selectedDate;
        dateInput.addEventListener('change', (e) => {
          this.selectedDate = e.target.value;
          this.loadSummary();
          this.loadFloorRooms(this.currentFloor);
          this.executeSearch();
        });
      }
    },

    // Choose a valid default search window inside operating hours (avoids 23:00->00:00 or after-hours blank screen on initial visit)
    alignInitialSearchWindow: function () {
      const startInput = document.getElementById('searchStartTime');
      const endInput = document.getElementById('searchEndTime');
      if (!startInput || !endInput) return;

      const opStartH = this.settings ? Math.floor((this.settings.operating_start_minutes || 480) / 60) : 8;
      const opEndH = this.settings ? Math.floor((this.settings.operating_end_minutes || 1080) / 60) : 18;

      const now = new Date();
      const currH = now.getHours();

      if (currH >= opStartH && currH + 1 <= opEndH) {
        startInput.value = `${String(currH).padStart(2, '0')}:00`;
        endInput.value = `${String(currH + 1).padStart(2, '0')}:00`;
        this.highlightPreset('now');
      } else {
        startInput.value = '10:00';
        endInput.value = '11:00';
        this.highlightPreset('morning');
      }
    },

    highlightPreset: function (presetName) {
      document.querySelectorAll('.preset-chip').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.preset === presetName);
      });
    },

    // Real-time campus clock
    startClock: function () {
      const update = () => {
        const now = new Date();
        const days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
        const day = days[now.getDay()];
        const timeStr = now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        const clockEl = document.getElementById('liveClockText');
        if (clockEl) {
          clockEl.textContent = `${day}, ${timeStr}`;
        }
      };
      update();
      setInterval(update, 15000);
    },

    // Load Public Settings
    loadSettings: async function () {
      try {
        const res = await fetch(apiUrl('/api/settings'));
        if (!res.ok) throw new Error('Failed to load settings');
        this.settings = await res.json();

        const bTitle = document.getElementById('heroBuildingTitle');
        const cTitle = document.getElementById('heroCollegeTitle');
        const fbCollege = document.getElementById('footerCollege');
        const fbBuilding = document.getElementById('footerBuilding');
        const fbFloors = document.getElementById('footerFloors');
        const fbHours = document.getElementById('footerHours');

        if (bTitle) bTitle.textContent = this.settings.building_name;
        if (cTitle) cTitle.textContent = this.settings.college_name;
        if (fbCollege) fbCollege.textContent = this.settings.college_name;
        if (fbBuilding) fbBuilding.textContent = this.settings.building_name;
        if (fbFloors) fbFloors.textContent = `${this.settings.total_floors} Configured Floors`;
        if (fbHours) fbHours.textContent = `Operating Hours: ${this.settings.operating_start} - ${this.settings.operating_end}`;
      } catch (err) {
        console.warn('Could not load settings:', err);
      }
    },

    // Load Campus Dynamic Summary
    loadSummary: async function () {
      try {
        const dateParam = this.selectedDate || '';
        const res = await fetch(apiUrl(`/api/summary?date=${encodeURIComponent(dateParam)}`));
        if (!res.ok) throw new Error('Failed to load summary');
        const data = await res.json();

        const elTotal = document.getElementById('summaryTotalRooms');
        const elAvail = document.getElementById('summaryAvailableRooms');
        const elOcc = document.getElementById('summaryOccupiedRooms');
        const elFloors = document.getElementById('summaryTotalFloors');
        const elBadge = document.getElementById('heroTermBadge');

        if (elTotal) elTotal.textContent = data.total_rooms;
        if (elAvail) elAvail.textContent = data.available_rooms;
        if (elOcc) elOcc.textContent = data.occupied_rooms;
        if (elFloors) elFloors.textContent = data.total_floors;

        if (elBadge) {
          if (data.outside_operating_hours) {
            elBadge.textContent = `Closed (${data.operating_start}–${data.operating_end})`;
            elBadge.style.background = 'var(--status-unavail-bg)';
            elBadge.style.color = 'var(--status-unavail-text)';
            elBadge.style.borderColor = 'var(--status-unavail-border)';
          } else {
            elBadge.textContent = 'Campus Open';
            elBadge.style.background = 'var(--status-avail-bg)';
            elBadge.style.color = 'var(--status-avail-text)';
            elBadge.style.borderColor = 'var(--status-avail-border)';
          }
        }
      } catch (err) {
        console.warn('Could not load summary:', err);
      }
    },

    // Load Floors & Build Directory
    loadFloors: async function () {
      try {
        const res = await fetch(apiUrl('/api/floors'));
        if (!res.ok) throw new Error('Failed to load floors');
        this.floors = await res.json();

        const levelsBadge = document.getElementById('buildingLevelsBadge');
        if (levelsBadge) {
          levelsBadge.textContent = `${this.floors.length} Level${this.floors.length === 1 ? '' : 's'}`;
        }

        // Populate search floor selector
        const floorSelect = document.getElementById('searchFloor');
        if (floorSelect) {
          const prevVal = floorSelect.value || '0';
          floorSelect.innerHTML = `<option value="0">All Floors (${this.floors.length} Levels)</option>`;
          this.floors.forEach(f => {
            const opt = document.createElement('option');
            opt.value = f.floor_number;
            opt.textContent = `Floor ${f.floor_number} — ${f.floor_name}`;
            floorSelect.appendChild(opt);
          });
          floorSelect.value = prevVal;
        }

        // Build Building Stack Directory (Ordered top floor down to Floor 1)
        const stackList = document.getElementById('floorStackList');
        if (stackList) {
          stackList.innerHTML = '';
          const sorted = [...this.floors].sort((a, b) => b.floor_number - a.floor_number);

          sorted.forEach(f => {
            const roomCount = (typeof f.room_count === 'number') ? f.room_count : 0;
            const item = document.createElement('div');
            item.className = `floor-row-item ${f.floor_number === this.currentFloor ? 'active' : ''}`;
            item.id = `floorRowItem_${f.floor_number}`;
            item.setAttribute('role', 'button');
            item.setAttribute('tabindex', '0');
            item.onclick = () => this.selectFloor(f.floor_number);
            item.onkeydown = (e) => {
              if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                this.selectFloor(f.floor_number);
              }
            };

            item.innerHTML = `
              <div class="floor-item-left">
                <div class="floor-num-badge">L${f.floor_number}</div>
                <div class="floor-item-text">
                  <div class="floor-item-title">Floor ${f.floor_number}</div>
                  <div class="floor-item-desc">${this.escapeHtml(f.floor_name)}</div>
                </div>
              </div>
              <div class="floor-item-count">${roomCount} Room${roomCount === 1 ? '' : 's'}</div>
            `;
            stackList.appendChild(item);
          });
        }

        // Build Hero Mini Stack with real room counts
        const miniStack = document.getElementById('heroMiniStack');
        if (miniStack && this.floors.length > 0) {
          miniStack.innerHTML = '';
          const sorted = [...this.floors].sort((a, b) => b.floor_number - a.floor_number);
          const pickIndices = sorted.length > 5
            ? [0, Math.floor(sorted.length / 4), Math.floor(sorted.length / 2), sorted.length - 2, sorted.length - 1]
            : sorted.map((_, idx) => idx);
          const uniqueFloors = [...new Set(pickIndices)].map(i => sorted[i]).filter(Boolean);

          uniqueFloors.forEach(f => {
            const roomCount = (typeof f.room_count === 'number') ? f.room_count : 0;
            const shortName = f.floor_name.includes('-') ? f.floor_name.split('-').slice(1).join('-').trim() : f.floor_name;
            const row = document.createElement('div');
            row.className = 'mini-floor-row';
            row.style.cursor = 'pointer';
            row.onclick = () => {
              this.selectFloor(f.floor_number);
              document.getElementById('building-section')?.scrollIntoView({ behavior: 'smooth' });
            };
            row.innerHTML = `
              <span class="mini-floor-name">Floor ${f.floor_number} · ${this.escapeHtml(shortName)}</span>
              <span class="mini-indicator">${roomCount} rooms</span>
            `;
            miniStack.appendChild(row);
          });
        }

      } catch (err) {
        console.error('Error loading floors:', err);
        this.showToast('Unable to load building floors. Please check backend connection.', 'error');
      }
    },

    // Select Floor & Load Details
    selectFloor: async function (floorNumber) {
      this.currentFloor = floorNumber;

      document.querySelectorAll('.floor-row-item').forEach(el => {
        el.classList.remove('active');
      });
      const activeEl = document.getElementById(`floorRowItem_${floorNumber}`);
      if (activeEl) activeEl.classList.add('active');

      const floorObj = this.floors.find(f => f.floor_number === floorNumber);
      if (floorObj) {
        const badge = document.getElementById('selectedFloorBadge');
        const title = document.getElementById('selectedFloorTitle');
        const desc = document.getElementById('selectedFloorDesc');

        if (badge) badge.textContent = `Floor ${floorNumber} Selected`;
        if (title) title.textContent = `Floor ${floorNumber} — ${floorObj.floor_name}`;
        if (desc) desc.textContent = floorObj.description || 'Configured lecture halls and tutorial spaces.';
      }

      await this.loadFloorRooms(floorNumber);
    },

    // Load Room Statuses for Selected Floor
    loadFloorRooms: async function (floorNumber) {
      const container = document.getElementById('floorRoomsGrid');
      if (!container) return;
      container.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 30px; color: var(--text-muted);">Loading classrooms...</div>';

      try {
        const dateParam = this.selectedDate || '';
        const res = await fetch(apiUrl(`/api/floors/${floorNumber}/rooms?date=${encodeURIComponent(dateParam)}`));
        if (!res.ok) throw new Error('Failed to load floor rooms');
        const rooms = await res.json();

        let availCount = 0;
        let occCount = 0;

        container.innerHTML = '';
        if (rooms.length === 0) {
          container.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 30px; color: var(--text-muted);">No classrooms configured for this floor.</div>';
        } else {
          rooms.forEach(r => {
            if (r.status === 'AVAILABLE') availCount++;
            if (r.status === 'OCCUPIED') occCount++;
            container.appendChild(this.createRoomCard(r));
          });
        }

        const elAvail = document.getElementById('floorCountAvail');
        const elOcc = document.getElementById('floorCountOcc');
        if (elAvail) elAvail.textContent = `${availCount} Available`;
        if (elOcc) elOcc.textContent = `${occCount} Occupied`;

      } catch (err) {
        console.error('Error loading floor rooms:', err);
        container.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 30px; color: var(--status-occ);">Failed to load floor rooms. Please ensure backend is running.</div>';
      }
    },

    // Create Room Card Element
    createRoomCard: function (room) {
      const card = document.createElement('div');
      card.className = 'room-card';

      let statusBadgeClass = 'status-unavailable';
      let statusText = room.status_label || 'Unavailable';
      if (room.status === 'AVAILABLE') {
        statusBadgeClass = 'status-available';
        statusText = 'Available';
      } else if (room.status === 'OCCUPIED') {
        statusBadgeClass = 'status-occupied';
        statusText = 'Occupied';
      } else if (!room.has_schedule_data) {
        statusBadgeClass = 'status-unavailable';
        statusText = 'No Schedule Data';
      }

      let currentInfoHtml = '';
      if (room.status === 'OCCUPIED') {
        currentInfoHtml = `
          <div class="room-current-info" style="border-left: 3px solid var(--status-occ);">
            <span class="info-label">Active Lecture (${this.escapeHtml(room.current_time_slot)})</span>
            <div class="info-value">${this.escapeHtml(room.current_subject || 'Scheduled Class')}</div>
            <div style="font-size: 0.78rem; color: var(--text-muted); margin-top: 2px;">Section: ${this.escapeHtml(room.current_class || '--')}</div>
          </div>
        `;
      } else if (room.status === 'AVAILABLE') {
        currentInfoHtml = `
          <div class="room-current-info" style="border-left: 3px solid var(--status-avail);">
            <span class="info-label">Current Status</span>
            <div class="info-value" style="color: var(--status-avail-text);">Classroom Free for Use</div>
            <div style="font-size: 0.78rem; color: var(--text-muted); margin-top: 2px;">${this.escapeHtml(room.next_available_slot || 'Free')}</div>
          </div>
        `;
      } else {
        currentInfoHtml = `
          <div class="room-current-info" style="border-left: 3px solid var(--status-unavail);">
            <span class="info-label">Timetable Coverage</span>
            <div class="info-value" style="color: var(--status-unavail-text);">${this.escapeHtml(room.status_label || 'Not Configured')}</div>
          </div>
        `;
      }

      card.innerHTML = `
        <div class="room-card-top">
          <div class="room-number-group">
            <span class="room-number">${this.escapeHtml(room.room_number)}</span>
            <span class="room-floor-tag">Floor ${room.floor_number}</span>
          </div>
          <span class="status-badge ${statusBadgeClass}">
            <span class="status-indicator-dot"></span>
            ${this.escapeHtml(statusText)}
          </span>
        </div>

        <div class="room-details-body">
          <div class="room-type-capacity">
            <span>${this.escapeHtml(room.room_type || 'Standard Classroom')}</span>
            <span>·</span>
            <span>Capacity: ${room.capacity || 60}</span>
          </div>

          ${currentInfoHtml}

          ${room.status === 'OCCUPIED' && room.next_available_slot ? `
            <div class="room-next-slot">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
              ${this.escapeHtml(room.next_available_slot)}
            </div>
          ` : ''}
        </div>

        <div class="room-card-footer">
          <button type="button" class="room-action-btn" data-room="${this.escapeHtml(room.room_number)}">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/></svg>
            View Day Schedule
          </button>
        </div>
      `;

      const actionBtn = card.querySelector('.room-action-btn');
      if (actionBtn) {
        actionBtn.addEventListener('click', () => this.openRoomModal(room.room_number));
      }

      return card;
    },

    // Execute Quick Availability Search
    executeSearch: async function () {
      const dateVal = document.getElementById('searchDate')?.value || this.selectedDate;
      const startTime = document.getElementById('searchStartTime')?.value || '09:00';
      const endTime = document.getElementById('searchEndTime')?.value || '10:00';
      const floorVal = document.getElementById('searchFloor')?.value || '0';
      const fullDayMode = document.getElementById('fullDayBreakdownToggle')?.checked || false;

      const resultsGrid = document.getElementById('searchResultsGrid');
      const emptyState = document.getElementById('searchEmptyState');
      const emptyTitle = document.getElementById('emptyStateTitle');
      const emptyMsg = document.getElementById('emptyStateMessage');
      const countBadge = document.getElementById('resultsCountBadge');
      const headerTitle = document.getElementById('resultsHeaderTitle');
      const headerSubtitle = document.getElementById('resultsHeaderSubtitle');

      if (!resultsGrid) return;

      if (!fullDayMode && startTime >= endTime) {
        this.showToast('Start time must be earlier than end time.', 'error');
        resultsGrid.innerHTML = '';
        if (countBadge) countBadge.textContent = '0 Free Classrooms';
        if (emptyTitle) emptyTitle.textContent = 'Invalid Time Range';
        if (emptyMsg) emptyMsg.textContent = 'Start time must be earlier than end time. Please adjust the interval and search again.';
        if (emptyState) emptyState.style.display = 'block';
        return;
      }

      resultsGrid.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 40px; color: var(--text-muted);">Searching available rooms...</div>';
      if (emptyState) emptyState.style.display = 'none';

      try {
        if (fullDayMode) {
          const url = apiUrl(`/api/daily-availability?date=${encodeURIComponent(dateVal)}&floor=${encodeURIComponent(floorVal)}`);
          const res = await fetch(url);
          if (!res.ok) {
            const errJson = await res.json().catch(() => ({}));
            throw new Error(errJson.error || 'Search failed');
          }
          const data = await res.json();

          headerTitle.textContent = `Full-Day Availability Schedule (${data.day}, ${dateVal})`;
          headerSubtitle.textContent = 'Displaying comprehensive free and occupied slot breakdown throughout college operating hours.';
          countBadge.textContent = `${data.results.length} Classrooms`;

          resultsGrid.innerHTML = '';
          if (data.results.length === 0) {
            if (emptyTitle) emptyTitle.textContent = 'No Scheduled Classrooms Found';
            if (emptyMsg) emptyMsg.textContent = 'No classrooms with verified timetable data were found for the selected floor.';
            if (emptyState) emptyState.style.display = 'block';
            return;
          }

          data.results.forEach(item => {
            const card = document.createElement('div');
            card.className = 'room-card';

            let slotsHtml = '';
            item.slots.forEach(slot => {
              const isAvail = slot.status === 'AVAILABLE';
              const label = isAvail ? 'AVAILABLE' : (slot.subject ? this.escapeHtml(slot.subject) : 'Occupied');
              slotsHtml += `
                <div style="display: flex; justify-content: space-between; align-items: center; gap: 8px; padding: 5px 8px; margin-bottom: 4px; border-radius: var(--radius-sm); font-size: 0.8rem; background: ${isAvail ? 'var(--status-avail-bg)' : 'var(--status-occ-bg)'}; color: ${isAvail ? 'var(--status-avail-text)' : 'var(--status-occ-text)'}; border: 1px solid ${isAvail ? 'var(--status-avail-border)' : 'var(--status-occ-border)'};">
                  <span style="font-weight: 700; white-space: nowrap;">${this.escapeHtml(slot.start_time)} - ${this.escapeHtml(slot.end_time)}</span>
                  <span style="font-weight: 600; text-align: right; overflow: hidden; text-overflow: ellipsis;">${label}</span>
                </div>
              `;
            });

            card.innerHTML = `
              <div class="room-card-top">
                <div class="room-number-group">
                  <span class="room-number">${this.escapeHtml(item.room_number)}</span>
                  <span class="room-floor-tag">Floor ${item.floor_number}</span>
                </div>
                <span class="status-badge status-available">Full Day</span>
              </div>
              <div class="room-type-capacity">
                <span>${this.escapeHtml(item.room_type || 'Classroom')}</span>
                <span>·</span>
                <span>Capacity: ${item.capacity}</span>
              </div>
              <div style="margin: 12px 0; max-height: 220px; overflow-y: auto;">
                ${slotsHtml}
              </div>
              <div class="room-card-footer">
                <button type="button" class="room-action-btn">Full Details</button>
              </div>
            `;
            card.querySelector('.room-action-btn')?.addEventListener('click', () => this.openRoomModal(item.room_number));
            resultsGrid.appendChild(card);
          });

        } else {
          const url = apiUrl(`/api/availability?date=${encodeURIComponent(dateVal)}&start_time=${encodeURIComponent(startTime)}&end_time=${encodeURIComponent(endTime)}&floor=${encodeURIComponent(floorVal)}`);
          const res = await fetch(url);
          if (!res.ok) {
            const errJson = await res.json().catch(() => ({}));
            throw new Error(errJson.error || 'Search request error');
          }
          const data = await res.json();

          headerTitle.textContent = `Available Classrooms: ${data.start_time_12h} – ${data.end_time_12h}`;
          headerSubtitle.textContent = data.outside_operating_hours
            ? `Requested slot falls outside campus operating hours (${data.operating_start} – ${data.operating_end}).`
            : `Zero timetable overlaps detected for ${data.day}, ${data.date}. Sorted by floor and room.`;
          countBadge.textContent = `${data.total_matches} Free Classrooms`;

          resultsGrid.innerHTML = '';
          if (data.total_matches === 0) {
            if (data.outside_operating_hours) {
              if (emptyTitle) emptyTitle.textContent = 'Outside College Operating Hours';
              if (emptyMsg) emptyMsg.textContent = `Campus operating hours are ${data.operating_start} to ${data.operating_end}. Please choose a time interval within operating hours.`;
            } else {
              if (emptyTitle) emptyTitle.textContent = 'No Classrooms Available';
              if (emptyMsg) emptyMsg.textContent = 'Every configured classroom on the selected floor is occupied during this interval. Try selecting a different time or all floors.';
            }
            if (emptyState) emptyState.style.display = 'block';
            return;
          }

          data.rooms.forEach(room => {
            const card = document.createElement('div');
            card.className = 'room-card';

            card.innerHTML = `
              <div class="room-card-top">
                <div class="room-number-group">
                  <span class="room-number">${this.escapeHtml(room.room_number)}</span>
                  <span class="room-floor-tag">Floor ${room.floor_number}</span>
                </div>
                <span class="status-badge status-available">
                  <span class="status-indicator-dot"></span>
                  Free
                </span>
              </div>

              <div class="room-details-body">
                <div class="room-type-capacity">
                  <span>${this.escapeHtml(room.room_type || 'Standard Classroom')}</span>
                  <span>·</span>
                  <span>Capacity: ${room.capacity || 60}</span>
                </div>

                <div class="room-current-info" style="border-left: 3px solid var(--status-avail); background-color: var(--status-avail-bg);">
                  <span class="info-label" style="color: var(--status-avail-text);">Free For Requested Window</span>
                  <div class="info-value" style="color: var(--status-avail-text); font-size: 0.95rem;">
                    ${this.escapeHtml(data.start_time)} to ${this.escapeHtml(data.end_time)}
                  </div>
                </div>

                <div style="font-size: 0.78rem; color: var(--text-muted); margin-top: 8px;">
                  <div><strong>Preceding:</strong> ${this.escapeHtml(room.previous_class || 'None')}</div>
                  <div style="margin-top: 3px;"><strong>Following:</strong> ${this.escapeHtml(room.next_class || 'None')}</div>
                </div>
              </div>

              <div class="room-card-footer">
                <button type="button" class="room-action-btn">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/></svg>
                  Inspect Full Schedule
                </button>
              </div>
            `;
            card.querySelector('.room-action-btn')?.addEventListener('click', () => this.openRoomModal(room.room_number));
            resultsGrid.appendChild(card);
          });
        }

      } catch (err) {
        console.error('Search error:', err);
        this.showToast(err.message, 'error');
        resultsGrid.innerHTML = `<div style="grid-column: 1/-1; text-align: center; padding: 40px; color: var(--status-occ);">Error: ${this.escapeHtml(err.message)}</div>`;
      }
    },

    // Set Quick Preset Filter
    setPreset: function (preset, btnEl) {
      document.querySelectorAll('.preset-chip').forEach(btn => btn.classList.remove('active'));
      if (btnEl) btnEl.classList.add('active');
      else this.highlightPreset(preset);

      const startInput = document.getElementById('searchStartTime');
      const endInput = document.getElementById('searchEndTime');
      const fullDayToggle = document.getElementById('fullDayBreakdownToggle');

      if (preset === 'fullday') {
        if (fullDayToggle) fullDayToggle.checked = true;
        this.executeSearch();
        return;
      } else {
        if (fullDayToggle) fullDayToggle.checked = false;
      }

      const now = new Date();
      const currH = now.getHours();

      if (preset === 'now') {
        const startH = Math.min(currH, 22);
        startInput.value = `${String(startH).padStart(2, '0')}:00`;
        endInput.value = `${String(startH + 1).padStart(2, '0')}:00`;
      } else if (preset === 'next_hour') {
        const startH = Math.min(currH + 1, 22);
        startInput.value = `${String(startH).padStart(2, '0')}:00`;
        endInput.value = `${String(startH + 1).padStart(2, '0')}:00`;
      } else if (preset === 'morning') {
        startInput.value = '10:00';
        endInput.value = '11:00';
      } else if (preset === 'afternoon') {
        startInput.value = '14:00';
        endInput.value = '15:00';
      }

      this.executeSearch();
    },

    toggleFullDayView: function () {
      this.executeSearch();
    },

    formatMinutes12h: function (mins) {
      const h = Math.floor(mins / 60) % 24;
      const m = mins % 60;
      const period = h >= 12 ? 'PM' : 'AM';
      const dispH = h % 12 === 0 ? 12 : h % 12;
      return `${String(dispH).padStart(2, '0')}:${String(m).padStart(2, '0')} ${period}`;
    },

    // Open Room Schedule Modal
    openRoomModal: async function (roomNumber) {
      const modal = document.getElementById('roomModalOverlay');
      if (!modal) return;

      const dateVal = document.getElementById('searchDate')?.value || this.selectedDate;

      try {
        const res = await fetch(apiUrl(`/api/rooms/${encodeURIComponent(roomNumber)}/schedule?date=${encodeURIComponent(dateVal)}`));
        if (!res.ok) throw new Error('Could not fetch room schedule');
        const data = await res.json();

        document.getElementById('modalRoomTitle').textContent = `Classroom ${data.room_number} — Timetable`;
        document.getElementById('modalRoomType').textContent = data.room_type || 'Classroom';
        document.getElementById('modalRoomLocation').textContent = `Floor ${data.floor_number} · Capacity: ${data.capacity} Students`;
        document.getElementById('modalDateDay').textContent = `${data.day}, ${data.date}`;

        const opStart = data.operating_start_minutes ?? this.settings?.operating_start_minutes ?? 480;
        const opEnd = data.operating_end_minutes ?? this.settings?.operating_end_minutes ?? 1080;
        const totalSpan = Math.max(1, opEnd - opStart);

        const timelineTitle = document.getElementById('modalTimelineTitle');
        if (timelineTitle) {
          timelineTitle.textContent = `Daily Timeline Visualization (${data.operating_start || '08:00'} to ${data.operating_end || '18:00'})`;
        }
        const timelineLabels = document.getElementById('modalTimelineLabels');
        if (timelineLabels) {
          const step = Math.round(totalSpan / 3);
          timelineLabels.innerHTML = `
            <span>${this.formatMinutes12h(opStart)}</span>
            <span>${this.formatMinutes12h(opStart + step)}</span>
            <span>${this.formatMinutes12h(opStart + step * 2)}</span>
            <span>${this.formatMinutes12h(opEnd)}</span>
          `;
        }

        const timelineBar = document.getElementById('modalTimelineBar');
        timelineBar.innerHTML = '';

        if (data.daily_slots && data.daily_slots.length > 0) {
          data.daily_slots.forEach(slot => {
            const startClamped = Math.max(opStart, slot.start_minutes);
            const endClamped = Math.min(opEnd, slot.end_minutes);
            const duration = endClamped - startClamped;

            if (duration > 0) {
              const pct = (duration / totalSpan) * 100;
              const seg = document.createElement('div');
              seg.className = `timeline-segment ${slot.status === 'AVAILABLE' ? 'timeline-avail' : 'timeline-occ'}`;
              seg.style.width = `${pct}%`;
              seg.title = `${slot.start_time} - ${slot.end_time}: ${slot.status}${slot.subject ? ' (' + slot.subject + ')' : ''}`;
              timelineBar.appendChild(seg);
            }
          });
        } else {
          timelineBar.innerHTML = '<div style="width: 100%; background: var(--status-unavail); display: flex; align-items: center; justify-content: center; font-size: 0.75rem; color: #fff;">No Schedule Data Configured</div>';
        }

        const tbody = document.getElementById('modalScheduleTableBody');
        tbody.innerHTML = '';

        if (!data.daily_slots || data.daily_slots.length === 0) {
          tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: var(--text-muted); padding: 20px;">No schedule data configured for this room.</td></tr>';
        } else {
          data.daily_slots.forEach(slot => {
            const isAvail = slot.status === 'AVAILABLE';
            const tr = document.createElement('tr');
            tr.innerHTML = `
              <td style="font-weight: 700; white-space: nowrap;">${this.escapeHtml(slot.start_time_12h || slot.start_time)} – ${this.escapeHtml(slot.end_time_12h || slot.end_time)}</td>
              <td>
                <span class="status-badge ${isAvail ? 'status-available' : 'status-occupied'}">
                  ${isAvail ? 'Available' : 'Occupied'}
                </span>
              </td>
              <td>${isAvail ? '—' : this.escapeHtml(slot.section || 'General')}</td>
              <td>${isAvail ? '<span style="color: var(--status-avail-text); font-weight: 600;">Free Study Window</span>' : this.escapeHtml(slot.subject || 'Lecture')}</td>
            `;
            tbody.appendChild(tr);
          });
        }

        modal.classList.add('active');

      } catch (err) {
        console.error('Modal schedule error:', err);
        this.showToast('Failed to load room schedule details.', 'error');
      }
    },

    closeModal: function () {
      const modal = document.getElementById('roomModalOverlay');
      if (modal) modal.classList.remove('active');
    },

    showToast: function (msg, type = 'info') {
      const container = document.getElementById('toastContainer');
      if (!container) return;

      const toast = document.createElement('div');
      toast.className = `toast toast-${type}`;
      toast.textContent = msg;

      container.appendChild(toast);
      setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
      }, 4000);
    },

    escapeHtml: function (str) {
      if (str === null || str === undefined) return '';
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }
  };

  window.RoomwiseApp = RoomwiseApp;
  document.addEventListener('DOMContentLoaded', () => RoomwiseApp.init());
})();
